/* Static Grok Bot / team list for the on-device menu (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bots.h"

#include <stdio.h>
#include <string.h>

/* const: default names stay in flash (.rodata). */
static const char *const kName[GROK_BOTS_COUNT] = {
    "Carty G",
    "The Chief",
    "Awesome Coder",
    "Chromatic Add-ons",
};

static char s_override[GROK_BOTS_COUNT][GROK_BOTS_NAME_MAX + 1];
static int s_selected;
static bool s_fast;

const char *grok_bots_default_name(int idx)
{
    if ((idx < 0) || (idx >= GROK_BOTS_COUNT))
    {
        return NULL;
    }
    return kName[idx];
}

const char *grok_bots_name(int idx)
{
    if ((idx < 0) || (idx >= GROK_BOTS_COUNT))
    {
        return NULL;
    }
    if (s_override[idx][0] != '\0')
    {
        return s_override[idx];
    }
    return kName[idx];
}

void grok_bots_set_name(int idx, const char *name)
{
    if ((idx < 0) || (idx >= GROK_BOTS_COUNT))
    {
        return;
    }
    if ((name == NULL) || (name[0] == '\0'))
    {
        s_override[idx][0] = '\0';
        return;
    }
    grok_bots_clip(s_override[idx], sizeof(s_override[idx]), name, GROK_BOTS_NAME_MAX);
}

void grok_bots_select(int idx)
{
    if ((idx < 0) || (idx >= GROK_BOTS_COUNT))
    {
        return;
    }
    s_selected = idx;
    s_fast = false;
}

int grok_bots_selected(void)
{
    return s_selected;
}

void grok_bots_set_fast(bool on)
{
    s_fast = on;
}

bool grok_bots_fast(void)
{
    return s_fast;
}

int grok_bots_menu_step(int cur, int delta, int menu_count)
{
    int n;

    if (menu_count <= 0)
    {
        return 0;
    }
    n = (cur + (delta % menu_count)) % menu_count;
    if (n < 0)
    {
        n += menu_count;
    }
    return n;
}

int grok_bots_step(int cur, int delta)
{
    return grok_bots_menu_step(cur, delta, GROK_BOTS_COUNT);
}

size_t grok_bots_format_row(char *out, size_t cap, int idx, bool selected,
                            size_t cols)
{
    size_t n = 0;
    size_t limit;
    const char *name;

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
    if (idx == GROK_BOTS_PHONE_IDX)
    {
        name = "Phone typing";
    }
    else if (idx == GROK_BOTS_ASK_IDX)
    {
        name = "Ask Grok";
    }
    else
    {
        name = grok_bots_name(idx);
    }
    if (name != NULL)
    {
        for (const char *p = name; (*p != '\0') && (n < limit); p++)
        {
            char ch = *p;
            out[n++] = ((ch >= 0x20) && (ch <= 0x7e)) ? ch : ' ';
        }
    }
    out[n] = '\0';
    return n;
}

void grok_bots_clip(char *dst, size_t dst_cap, const char *src, size_t max_len)
{
    size_t n = 0;
    size_t limit;

    if ((dst == NULL) || (dst_cap == 0))
    {
        return;
    }
    dst[0] = '\0';
    if (src == NULL)
    {
        return;
    }
    limit = max_len;
    if (limit >= dst_cap)
    {
        limit = dst_cap - 1;
    }
    for (const char *p = src; (*p != '\0') && (n < limit); p++)
    {
        char ch = *p;
        if ((ch == '\n') || (ch == '\r') || (ch == '\t'))
        {
            ch = ' ';
        }
        if ((ch < 0x20) || (ch > 0x7e))
        {
            ch = ' ';
        }
        dst[n++] = ch;
    }
    dst[n] = '\0';
}

size_t grok_bots_pack_last(char *out, size_t cap, const char *prompt,
                           const char *reply)
{
    char p[GROK_BOTS_LAST_PROMPT_MAX + 1];
    char r[GROK_BOTS_LAST_REPLY_MAX + 1];
    size_t pn;
    size_t rn;
    size_t need;

    if ((out == NULL) || (cap == 0))
    {
        return 0;
    }
    grok_bots_clip(p, sizeof(p), prompt, GROK_BOTS_LAST_PROMPT_MAX);
    grok_bots_clip(r, sizeof(r), reply, GROK_BOTS_LAST_REPLY_MAX);
    pn = strlen(p);
    rn = strlen(r);
    need = pn + 1 + rn + 1;
    if (need > cap)
    {
        out[0] = '\0';
        return 0;
    }
    memcpy(out, p, pn);
    out[pn] = '\n';
    memcpy(out + pn + 1, r, rn);
    out[pn + 1 + rn] = '\0';
    return pn + 1 + rn;
}

void grok_bots_unpack_last(const char *packed, char *prompt, size_t prompt_cap,
                           char *reply, size_t reply_cap)
{
    const char *nl;

    if ((prompt != NULL) && (prompt_cap > 0))
    {
        prompt[0] = '\0';
    }
    if ((reply != NULL) && (reply_cap > 0))
    {
        reply[0] = '\0';
    }
    if ((packed == NULL) || (packed[0] == '\0'))
    {
        return;
    }
    nl = strchr(packed, '\n');
    if (nl == NULL)
    {
        grok_bots_clip(prompt, prompt_cap, packed, GROK_BOTS_LAST_PROMPT_MAX);
        return;
    }
    {
        size_t plen = (size_t)(nl - packed);
        char tmp[GROK_BOTS_LAST_PROMPT_MAX + 1];

        if (plen > GROK_BOTS_LAST_PROMPT_MAX)
        {
            plen = GROK_BOTS_LAST_PROMPT_MAX;
        }
        memcpy(tmp, packed, plen);
        tmp[plen] = '\0';
        grok_bots_clip(prompt, prompt_cap, tmp, GROK_BOTS_LAST_PROMPT_MAX);
        grok_bots_clip(reply, reply_cap, nl + 1, GROK_BOTS_LAST_REPLY_MAX);
    }
}

size_t grok_bots_format_note(char *out, size_t cap, int idx,
                             const char *prompt, const char *reply)
{
    const char *name;
    char p[GROK_BOTS_LAST_PROMPT_MAX + 1];
    char r[GROK_BOTS_LAST_REPLY_MAX + 1];
    int n;

    if ((out == NULL) || (cap == 0))
    {
        return 0;
    }
    name = grok_bots_name(idx);
    if (name == NULL)
    {
        out[0] = '\0';
        return 0;
    }
    grok_bots_clip(p, sizeof(p), prompt, GROK_BOTS_LAST_PROMPT_MAX);
    grok_bots_clip(r, sizeof(r), reply, GROK_BOTS_LAST_REPLY_MAX);
    if (p[0] == '\0')
    {
        n = snprintf(out, cap, "%s\n\nWhere we left off:\n(nothing yet)", name);
    }
    else if (r[0] == '\0')
    {
        n = snprintf(out, cap, "%s\n\nWhere we left off:\n%s", name, p);
    }
    else
    {
        n = snprintf(out, cap, "%s\n\nWhere we left off:\n%s\n\n%s", name, p, r);
        if ((n < 0) || ((size_t)n >= cap))
        {
            n = snprintf(out, cap, "%s\n\nWhere we left off:\n%s", name, p);
        }
    }
    if (n < 0)
    {
        out[0] = '\0';
        return 0;
    }
    if ((size_t)n >= cap)
    {
        return cap - 1;
    }
    return (size_t)n;
}
