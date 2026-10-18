/* Portable helpers for the phone page. See grok_web_util.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_web_util.h"

#include <string.h>

void grok_web_pin_format(uint32_t r, char out[GROK_WEB_PIN_LEN + 1])
{
    uint32_t v = r % 1000000u;

    for (int i = GROK_WEB_PIN_LEN - 1; i >= 0; i--)
    {
        out[i] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    out[GROK_WEB_PIN_LEN] = '\0';
}

static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;

    while ((n < max) && (s[n] != '\0'))
    {
        n++;
    }
    return n;
}

bool grok_web_pin_equal(const char *want, const char *got)
{
    uint8_t diff = 0;
    size_t got_len;

    if ((want == NULL) || (got == NULL))
    {
        return false;
    }
    got_len = bounded_len(got, GROK_WEB_PIN_LEN + 1);
    if ((bounded_len(want, GROK_WEB_PIN_LEN + 1) != GROK_WEB_PIN_LEN) || (got_len != GROK_WEB_PIN_LEN))
    {
        return false;
    }
    /* Same work for every input of the right length. */
    for (size_t i = 0; i < GROK_WEB_PIN_LEN; i++)
    {
        diff |= (uint8_t)(want[i] ^ got[i]);
    }
    return diff == 0;
}

GrokWebPin_t grok_web_guard_check(GrokWebGuard_t *guard, int64_t now_ms, const char *want, const char *got)
{
    if (guard == NULL)
    {
        return GROK_WEB_PIN_BAD;
    }
    if (now_ms < guard->locked_until_ms)
    {
        return GROK_WEB_PIN_LOCKED;
    }
    if (grok_web_pin_equal(want, got))
    {
        guard->fails = 0;
        return GROK_WEB_PIN_OK;
    }
    guard->fails++;
    if (guard->fails >= GROK_WEB_MAX_FAILS)
    {
        guard->fails = 0;
        guard->locked_until_ms = now_ms + GROK_WEB_LOCK_MS;
        return GROK_WEB_PIN_LOCKED;
    }
    return GROK_WEB_PIN_BAD;
}

uint32_t grok_web_guard_wait_s(const GrokWebGuard_t *guard, int64_t now_ms)
{
    if ((guard == NULL) || (now_ms >= guard->locked_until_ms))
    {
        return 0;
    }
    return (uint32_t)((guard->locked_until_ms - now_ms + 999) / 1000);
}

static int hex_val(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool grok_web_form_get(const char *form, size_t form_len, const char *key, char *out, size_t out_cap)
{
    size_t key_len;
    size_t i = 0;

    if ((form == NULL) || (key == NULL) || (out == NULL) || (out_cap == 0))
    {
        return false;
    }
    out[0] = '\0';
    key_len = strlen(key);
    while (i < form_len)
    {
        size_t end = i;
        size_t used = 0;

        while ((end < form_len) && (form[end] != '&') && (form[end] != '\0'))
        {
            end++;
        }
        if (((end - i) > key_len) && (memcmp(form + i, key, key_len) == 0) && (form[i + key_len] == '='))
        {
            for (size_t j = i + key_len + 1; j < end; j++)
            {
                char c = form[j];

                if (c == '+')
                {
                    c = ' ';
                }
                else if (c == '%')
                {
                    int hi;
                    int lo;

                    if ((j + 2) >= end)
                    {
                        out[0] = '\0';
                        return false;
                    }
                    hi = hex_val(form[j + 1]);
                    lo = hex_val(form[j + 2]);
                    if ((hi < 0) || (lo < 0))
                    {
                        out[0] = '\0';
                        return false;
                    }
                    c = (char)((hi << 4) | lo);
                    j += 2;
                }
                if ((used + 1) >= out_cap)
                {
                    out[0] = '\0';
                    return false;
                }
                out[used++] = c;
            }
            out[used] = '\0';
            return true;
        }
        if ((end < form_len) && (form[end] == '\0'))
        {
            break;
        }
        i = end + 1;
    }
    return false;
}

size_t grok_web_sanitize(char *text)
{
    size_t w = 0;

    if (text == NULL)
    {
        return 0;
    }
    for (size_t r = 0; text[r] != '\0'; r++)
    {
        unsigned char c = (unsigned char)text[r];

        if ((c == '\r') || (c == '\n') || (c == '\t'))
        {
            c = ' ';
        }
        if ((c < 0x20) || (c > 0x7e))
        {
            continue;
        }
        if ((c == ' ') && ((w == 0) || (text[w - 1] == ' ')))
        {
            continue;
        }
        text[w++] = (char)c;
    }
    while ((w > 0) && (text[w - 1] == ' '))
    {
        w--;
    }
    text[w] = '\0';
    return w;
}
