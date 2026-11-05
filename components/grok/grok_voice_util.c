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

static void trim_trailing(char *s)
{
    size_t n;

    if (s == NULL)
    {
        return;
    }
    n = strlen(s);
    while ((n > 0) && ((s[n - 1] == '\r') || (s[n - 1] == '\n') ||
                       (s[n - 1] == ' ') || (s[n - 1] == '\t')))
    {
        s[--n] = '\0';
    }
}

static GrokVoiceEvt_t handle_complete_line(GrokVoiceParser_t *p, const char **line_out)
{
    trim_trailing(p->line);
    if (line_out != NULL)
    {
        *line_out = p->line;
    }

    if (strcmp(p->line, GROK_VOICE_BEGIN) == 0)
    {
        p->in_frame = true;
        p->body[0] = '\0';
        p->body_len = 0;
        return GROK_VOICE_EVT_BEGIN;
    }
    if (strcmp(p->line, GROK_VOICE_END) == 0)
    {
        p->in_frame = false;
        return GROK_VOICE_EVT_END;
    }
    if (p->in_frame)
    {
        size_t len = strlen(p->line);
        size_t need = len + ((p->body_len > 0) ? 1u : 0u);

        if ((p->body_len + need) >= GROK_VOICE_BODY_MAX)
        {
            return GROK_VOICE_EVT_OVERFLOW;
        }
        if (p->body_len > 0)
        {
            p->body[p->body_len++] = '\n';
        }
        memcpy(p->body + p->body_len, p->line, len);
        p->body_len += len;
        p->body[p->body_len] = '\0';
        return GROK_VOICE_EVT_BODY_LINE;
    }
    return GROK_VOICE_EVT_LINE;
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
    if (ch == '\n')
    {
        GrokVoiceEvt_t ev;

        p->line[p->line_len] = '\0';
        ev = handle_complete_line(p, line_out);
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
        return GROK_VOICE_EVT_OVERFLOW;
    }
    p->line[p->line_len++] = ch;
    p->line[p->line_len] = '\0';
    return GROK_VOICE_EVT_NONE;
}

GrokVoiceEvt_t grok_voice_parser_feed_line(GrokVoiceParser_t *p, const char *line)
{
    size_t i;

    if ((p == NULL) || (line == NULL))
    {
        return GROK_VOICE_EVT_NONE;
    }
    p->line_len = 0;
    p->line[0] = '\0';
    for (i = 0; line[i] != '\0'; i++)
    {
        char ch = line[i];

        if ((ch == '\r') || (ch == '\n'))
        {
            break;
        }
        if (p->line_len >= GROK_VOICE_LINE_MAX)
        {
            p->line_len = 0;
            p->line[0] = '\0';
            return GROK_VOICE_EVT_OVERFLOW;
        }
        p->line[p->line_len++] = ch;
    }
    p->line[p->line_len] = '\0';
    return handle_complete_line(p, NULL);
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
