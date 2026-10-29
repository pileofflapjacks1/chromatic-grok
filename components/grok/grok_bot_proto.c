/* Grok Bot webhook + relay helpers (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bot_proto.h"

#include "grok_text.h"

#include <stdio.h>
#include <string.h>

bool grok_bot_build_post_json(char *out, size_t cap, const char *text,
                              const char *device_id, const char *msg_id)
{
    size_t used = 0;
    size_t escaped = 0;
    int n;

    if ((out == NULL) || (cap == 0))
    {
        return false;
    }
    out[0] = '\0';
    if ((text == NULL) || (device_id == NULL) || (msg_id == NULL))
    {
        return false;
    }
    n = snprintf(out, cap, "{\"text\":\"");
    if ((n <= 0) || ((size_t)n >= cap))
    {
        return false;
    }
    used = (size_t)n;
    if (!grok_text_json_escape(out + used, cap - used, text, &escaped))
    {
        return false;
    }
    used += escaped;
    n = snprintf(out + used, cap - used, "\",\"device_id\":\"");
    if ((n <= 0) || ((size_t)n >= (cap - used)))
    {
        return false;
    }
    used += (size_t)n;
    if (!grok_text_json_escape(out + used, cap - used, device_id, &escaped))
    {
        return false;
    }
    used += escaped;
    n = snprintf(out + used, cap - used, "\",\"msg_id\":\"");
    if ((n <= 0) || ((size_t)n >= (cap - used)))
    {
        return false;
    }
    used += (size_t)n;
    if (!grok_text_json_escape(out + used, cap - used, msg_id, &escaped))
    {
        return false;
    }
    used += escaped;
    n = snprintf(out + used, cap - used, "\"}");
    return (n > 0) && ((size_t)n < (cap - used));
}

static void clip_ascii(char *dst, size_t cap, const char *src, size_t max_len)
{
    size_t n = 0;
    size_t limit;

    if ((dst == NULL) || (cap == 0))
    {
        return;
    }
    dst[0] = '\0';
    if (src == NULL)
    {
        return;
    }
    limit = max_len;
    if (limit >= cap)
    {
        limit = cap - 1;
    }
    for (const char *p = src; (*p != '\0') && (n < limit); p++)
    {
        char ch = *p;
        if ((ch < 0x20) || (ch > 0x7e))
        {
            ch = ' ';
        }
        dst[n++] = ch;
    }
    dst[n] = '\0';
}

bool grok_bot_parse_reply_json(const char *json, GrokBotReply_t *out)
{
    char tmp[80];
    const char *arr;
    int got = 0;

    if (out == NULL)
    {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if ((json == NULL) || (json[0] == '\0'))
    {
        return false;
    }
    if (!grok_text_json_get_string(json, "reply", tmp, sizeof(tmp)))
    {
        return false;
    }
    clip_ascii(out->reply, sizeof(out->reply), tmp, sizeof(out->reply) - 1);
    if (out->reply[0] == '\0')
    {
        return false;
    }

    arr = strstr(json, "\"suggestions\"");
    if (arr == NULL)
    {
        return true;
    }
    arr = strchr(arr, '[');
    if (arr == NULL)
    {
        return true;
    }
    arr++;
    while ((*arr != '\0') && (*arr != ']') && (got < GROK_BOT_SUGGEST_COUNT))
    {
        while ((*arr == ' ') || (*arr == ',') || (*arr == '\n') || (*arr == '\r') ||
               (*arr == '\t'))
        {
            arr++;
        }
        if (*arr == '"')
        {
            char sug[GROK_BOT_SUGGEST_MAX + 8];
            size_t n = 0;

            arr++;
            while ((*arr != '\0') && (*arr != '"') && (n + 1 < sizeof(sug)))
            {
                if ((*arr == '\\') && (arr[1] != '\0'))
                {
                    arr++;
                    if (*arr == 'n')
                    {
                        sug[n++] = ' ';
                        arr++;
                        continue;
                    }
                }
                sug[n++] = *arr++;
            }
            sug[n] = '\0';
            if (*arr == '"')
            {
                arr++;
            }
            clip_ascii(out->suggest[got], sizeof(out->suggest[got]), sug,
                       GROK_BOT_SUGGEST_MAX);
            if (out->suggest[got][0] != '\0')
            {
                got++;
            }
        }
        else if (*arr == ']')
        {
            break;
        }
        else if (*arr != '\0')
        {
            arr++;
        }
    }
    out->suggest_n = got;
    return true;
}

void grok_bot_poll_begin(GrokBotPoll_t *p, uint32_t now_ms, uint32_t interval_ms,
                         uint32_t timeout_ms)
{
    if (p == NULL)
    {
        return;
    }
    p->start_ms = now_ms;
    p->last_poll_ms = 0;
    p->interval_ms = (interval_ms == 0) ? GROK_BOT_POLL_INTERVAL_MS : interval_ms;
    p->timeout_ms = (timeout_ms == 0) ? GROK_BOT_POLL_TIMEOUT_MS : timeout_ms;
    p->started = true;
}

GrokBotPollAction_t grok_bot_poll_tick(GrokBotPoll_t *p, uint32_t now_ms)
{
    uint32_t elapsed;

    if ((p == NULL) || !p->started)
    {
        return GROK_BOT_POLL_IDLE;
    }
    elapsed = now_ms - p->start_ms;
    if (elapsed >= p->timeout_ms)
    {
        p->started = false;
        return GROK_BOT_POLL_TIMEOUT;
    }
    if ((p->last_poll_ms == 0) || ((now_ms - p->last_poll_ms) >= p->interval_ms))
    {
        p->last_poll_ms = now_ms;
        return GROK_BOT_POLL_DO_GET;
    }
    return GROK_BOT_POLL_IDLE;
}

bool grok_bot_build_poll_path(char *out, size_t cap, const char *device_id,
                              const char *msg_id)
{
    int n;

    if ((out == NULL) || (cap == 0) || (device_id == NULL) || (msg_id == NULL))
    {
        return false;
    }
    n = snprintf(out, cap, "/v1/reply?device_id=%s&msg_id=%s", device_id, msg_id);
    return (n > 0) && ((size_t)n < cap);
}

void grok_bot_mask_secret(char *out, size_t cap, const char *secret)
{
    size_t len;

    if ((out == NULL) || (cap == 0))
    {
        return;
    }
    if ((secret == NULL) || (secret[0] == '\0'))
    {
        snprintf(out, cap, "(unset)");
        return;
    }
    len = strlen(secret);
    if (len <= 8)
    {
        snprintf(out, cap, "****");
        return;
    }
    snprintf(out, cap, "%.4s…%.4s", secret, secret + len - 4);
}

size_t grok_bot_format_option(char *out, size_t cap, int idx, bool selected,
                              const GrokBotReply_t *reply, size_t cols)
{
    size_t n = 0;
    size_t limit;
    const char *label;

    if ((out == NULL) || (cap == 0))
    {
        return 0;
    }
    limit = (cols < (cap - 1)) ? cols : (cap - 1);
    if (limit < 3)
    {
        out[0] = '\0';
        return 0;
    }
    out[n++] = selected ? '>' : ' ';
    out[n++] = (char)('1' + ((idx >= 0) && (idx < 9) ? idx : 0));
    out[n++] = ' ';
    if ((idx >= 0) && (idx < GROK_BOT_SUGGEST_COUNT) && (reply != NULL) &&
        (reply->suggest[idx][0] != '\0'))
    {
        label = reply->suggest[idx];
    }
    else
    {
        label = GROK_BOT_WAIT_OPTION;
    }
    for (const char *p = label; (*p != '\0') && (n < limit); p++)
    {
        char ch = *p;
        out[n++] = ((ch >= 0x20) && (ch <= 0x7e)) ? ch : ' ';
    }
    out[n] = '\0';
    return n;
}
