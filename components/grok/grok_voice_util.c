/* Laptop-voice serial framing and caption chunking (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_voice_util.h"

#include <stdio.h>
#include <string.h>

void grok_voice_parser_init(GrokVoiceParser_t *p)
{
    if (p == NULL)
    {
        return;
    }
    memset(p, 0, sizeof(*p));
}

static void clear_body(GrokVoiceParser_t *p)
{
    p->body[0] = '\0';
    p->body_len = 0;
}

static void mark_fail(GrokVoiceParser_t *p)
{
    p->fail = true;
    clear_body(p);
}

static void close_frame(GrokVoiceParser_t *p)
{
    p->in_frame = false;
    p->fail = false;
}

static bool append_bytes(GrokVoiceParser_t *p, const char *src, size_t n)
{
    if (p->fail)
    {
        return false;
    }
    if (n > (GROK_VOICE_BODY_MAX - p->body_len))
    {
        mark_fail(p);
        return false;
    }
    if (n > 0)
    {
        memcpy(p->body + p->body_len, src, n);
        p->body_len += n;
    }
    p->body[p->body_len] = '\0';
    return true;
}

/* 1 = well formed, 0 = not a data line, -1 = data line with a bad length. */
static int parse_data_line(const char *line, const char **payload, size_t *out_len)
{
    const char *p;
    size_t n = 0;
    int digits = 0;

    if (strncmp(line, GROK_VOICE_DATA_PREFIX, strlen(GROK_VOICE_DATA_PREFIX)) != 0)
    {
        return 0;
    }
    p = line + strlen(GROK_VOICE_DATA_PREFIX);
    while ((*p >= '0') && (*p <= '9'))
    {
        size_t digit = (size_t)(*p - '0');

        if ((digits >= 3) || (n > ((GROK_VOICE_LINE_MAX - digit) / 10u)))
        {
            return -1;
        }
        n = (n * 10u) + digit;
        digits++;
        p++;
    }
    if ((digits == 0) || (*p != ' '))
    {
        return -1;
    }
    *payload = p + 1;
    *out_len = n;
    return 1;
}

static int is_bare_token(const char *line)
{
    return (strcmp(line, GROK_VOICE_END) == 0) ||
           (strcmp(line, GROK_VOICE_ABORT) == 0) ||
           (strcmp(line, GROK_VOICE_NL) == 0) ||
           (strncmp(line, GROK_VOICE_DATA_PREFIX, strlen(GROK_VOICE_DATA_PREFIX)) == 0);
}

static GrokVoiceEvt_t accept_line(GrokVoiceParser_t *p, const char *line)
{
    const char *payload = NULL;
    size_t want = 0;
    int parsed;

    if (strcmp(line, GROK_VOICE_BEGIN) == 0)
    {
        close_frame(p);
        p->in_frame = true;
        clear_body(p);
        return GROK_VOICE_EVT_BEGIN;
    }
    if (!p->in_frame)
    {
        /* A token with no open frame is not a console command. */
        if (is_bare_token(line))
        {
            return GROK_VOICE_EVT_HOLD;
        }
        return GROK_VOICE_EVT_LINE;
    }
    if (strcmp(line, GROK_VOICE_END) == 0)
    {
        if (p->fail)
        {
            clear_body(p);
            close_frame(p);
            return GROK_VOICE_EVT_DROPPED;
        }
        close_frame(p);
        return GROK_VOICE_EVT_END;
    }
    if (strcmp(line, GROK_VOICE_ABORT) == 0)
    {
        clear_body(p);
        close_frame(p);
        return GROK_VOICE_EVT_DROPPED;
    }

    /* Stay in the frame. A line cap must not fall through to esp_console. */
    if (p->fail)
    {
        return GROK_VOICE_EVT_HOLD;
    }
    if (strcmp(line, GROK_VOICE_NL) == 0)
    {
        if (!append_bytes(p, "\n", 1))
        {
            return GROK_VOICE_EVT_HOLD;
        }
        return GROK_VOICE_EVT_BODY_LINE;
    }
    parsed = parse_data_line(line, &payload, &want);
    if (parsed == 1)
    {
        if (strlen(payload) != want)
        {
            mark_fail(p);
            return GROK_VOICE_EVT_HOLD;
        }
        if (!append_bytes(p, payload, want))
        {
            return GROK_VOICE_EVT_HOLD;
        }
        return GROK_VOICE_EVT_BODY_LINE;
    }
    /* Raw reply text, including "wifi clear" or a marker word, is not a command. */
    mark_fail(p);
    return GROK_VOICE_EVT_HOLD;
}

GrokVoiceEvt_t grok_voice_parser_feed(GrokVoiceParser_t *p, char ch,
                                      const char **line_out)
{
    if (p == NULL)
    {
        return GROK_VOICE_EVT_NONE;
    }
    if (line_out != NULL)
    {
        *line_out = NULL;
    }
    if (ch == '\r')
    {
        return GROK_VOICE_EVT_NONE;
    }
    if (p->skip_rest)
    {
        if (ch == '\n')
        {
            p->skip_rest = false;
            p->line_len = 0;
            p->line[0] = '\0';
        }
        return GROK_VOICE_EVT_NONE;
    }
    if (ch == '\n')
    {
        GrokVoiceEvt_t ev;

        p->line[p->line_len] = '\0';
        ev = accept_line(p, p->line);
        if (line_out != NULL)
        {
            *line_out = p->line;
        }
        p->line_len = 0;
        p->line[0] = '\0';
        return ev;
    }
    if ((ch < 0x20) || (ch > 0x7e))
    {
        return GROK_VOICE_EVT_NONE;
    }
    if (p->line_len >= GROK_VOICE_LINE_MAX)
    {
        p->line_len = 0;
        p->line[0] = '\0';
        if (p->in_frame)
        {
            mark_fail(p);
            p->skip_rest = true;
            return GROK_VOICE_EVT_HOLD;
        }
        return GROK_VOICE_EVT_OVERFLOW;
    }
    p->line[p->line_len++] = ch;
    p->line[p->line_len] = '\0';
    return GROK_VOICE_EVT_NONE;
}

GrokVoiceEvt_t grok_voice_parser_feed_line(GrokVoiceParser_t *p, const char *line)
{
    size_t n = 0;

    if ((p == NULL) || (line == NULL))
    {
        return GROK_VOICE_EVT_NONE;
    }
    while ((line[n] != '\0') && (line[n] != '\r') && (line[n] != '\n'))
    {
        n++;
        if (n > GROK_VOICE_LINE_MAX)
        {
            if (p->in_frame)
            {
                mark_fail(p);
                return GROK_VOICE_EVT_HOLD;
            }
            return GROK_VOICE_EVT_LINE;
        }
    }
    memcpy(p->line, line, n);
    p->line[n] = '\0';
    p->line_len = n;
    return accept_line(p, p->line);
}

bool grok_voice_is_cmd_line(const char *line)
{
    if (line == NULL)
    {
        return false;
    }
    while ((*line == ' ') || (*line == '\t'))
    {
        line++;
    }
    if (strncmp(line, "voice", 5) != 0)
    {
        return false;
    }
    return (line[5] == '\0') || (line[5] == ' ') || (line[5] == '\t');
}

static int is_punct_end(char ch)
{
    return (ch == '.') || (ch == '!') || (ch == '?');
}

size_t grok_voice_chunk_captions(const char *text, char *storage,
                                 size_t chunk_stride, size_t max_chunks,
                                 size_t cols)
{
    size_t count = 0;
    const char *p;
    char folded[GROK_REPLY_MAX + 1];
    size_t max_chars;

    if ((text == NULL) || (storage == NULL) || (chunk_stride < 2) ||
        (max_chunks == 0) || (cols < 1))
    {
        return 0;
    }
    /* One phrase fits a few rows of the 160x144 unscii_8 overlay. */
    max_chars = cols * 4;
    if (max_chars + 1 > chunk_stride)
    {
        max_chars = chunk_stride - 1;
    }
    snprintf(folded, sizeof(folded), "%s", text);
    grok_text_fold_ascii(folded);
    p = folded;
    while ((*p != '\0') && (count < max_chunks))
    {
        char *dst = storage + (count * chunk_stride);
        size_t used = 0;
        size_t last_space = 0;
        size_t cut = 0;

        while ((*p == ' ') || (*p == '\n') || (*p == '\t'))
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        while ((*p != '\0') && (used < max_chars))
        {
            char ch = *p++;

            if (ch == '\n')
            {
                if (used > 0)
                {
                    cut = used;
                    break;
                }
                continue;
            }
            dst[used++] = ch;
            if (ch == ' ')
            {
                last_space = used;
            }
            if (is_punct_end(ch) && (used >= cols))
            {
                cut = used;
                break;
            }
        }
        if (cut == 0)
        {
            if ((*p == '\0') || (used < max_chars))
            {
                cut = used;
            }
            else if (last_space > 0)
            {
                /* Rewind to last space so the next chunk starts on a word. */
                size_t rewind = used - last_space;

                cut = last_space;
                while ((cut > 0) && (dst[cut - 1] == ' '))
                {
                    cut--;
                }
                p -= rewind;
            }
            else
            {
                cut = used;
            }
        }
        while ((cut > 0) && (dst[cut - 1] == ' '))
        {
            cut--;
        }
        dst[cut] = '\0';
        if (cut == 0)
        {
            break;
        }
        count++;
    }
    return count;
}

size_t grok_voice_format_bot_line(char *out, size_t out_cap, const char *name)
{
    int n;

    if ((out == NULL) || (out_cap == 0))
    {
        return 0;
    }
    n = snprintf(out, out_cap, "%s%s\n", GROK_VOICE_BOT_PREFIX,
                 ((name != NULL) && (name[0] != '\0')) ? name : "?");
    if (n < 0)
    {
        out[0] = '\0';
        return 0;
    }
    if ((size_t)n >= out_cap)
    {
        return out_cap - 1;
    }
    return (size_t)n;
}

const char *grok_voice_status_text(GrokVoiceStatus_t status)
{
    switch (status)
    {
    case GROK_VOICE_STATUS_LISTENING:
        return "Listening";
    case GROK_VOICE_STATUS_THINKING:
        return "Thinking";
    default:
        return "Reply";
    }
}

size_t grok_voice_title_cols(GrokVoiceStatus_t status)
{
    size_t used = strlen(grok_voice_status_text(status)) + 1u;

    return (used < GROK_VOICE_HEAD_COLS) ? (GROK_VOICE_HEAD_COLS - used) : 0u;
}

size_t grok_voice_format_title(char *out, size_t out_cap, const char *name, size_t max_cols)
{
    size_t len;
    size_t keep;

    if ((out == NULL) || (out_cap == 0))
    {
        return 0;
    }
    if ((name == NULL) || (name[0] == '\0'))
    {
        name = "?";
    }
    len = strlen(name);
    if (max_cols >= out_cap)
    {
        max_cols = out_cap - 1u;
    }
    if (len <= max_cols)
    {
        memcpy(out, name, len + 1u);
        return len;
    }
    if (max_cols == 0)
    {
        out[0] = '\0';
        return 0;
    }
    keep = max_cols - 1u;
    memcpy(out, name, keep);
    out[keep] = '.';
    out[keep + 1u] = '\0';
    return keep + 1u;
}

size_t grok_voice_format_page(char *out, size_t out_cap, size_t page, size_t pages)
{
    int n;

    if ((out == NULL) || (out_cap == 0))
    {
        return 0;
    }
    if (pages == 0)
    {
        pages = 1;
    }
    if (page >= pages)
    {
        page = pages - 1u;
    }
    n = snprintf(out, out_cap, "%u/%u", (unsigned)(page + 1u), (unsigned)pages);
    if (n < 0)
    {
        out[0] = '\0';
        return 0;
    }
    if ((size_t)n >= out_cap)
    {
        return out_cap - 1u;
    }
    return (size_t)n;
}
