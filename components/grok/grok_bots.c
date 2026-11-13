/* Static Grok Bot / team list for the on-device menu (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bots.h"

#include <stdio.h>
#include <string.h>

/* const: names and notes stay in flash (.rodata). No network; no Grok Bot API. */
static const char *const kName[GROK_BOTS_COUNT] = {
    "Carty G",
    "The Chief",
    "Awesome Coder",
    "Chromatic Add-ons",
};

/* Per-bot "where we left off" placeholder. Empty until a later PR stores notes. */
static const char *const kNote[GROK_BOTS_COUNT] = {
    "",
    "",
    "",
    "",
};

const char *grok_bots_name(int idx)
{
    if ((idx < 0) || (idx >= GROK_BOTS_COUNT))
    {
        return NULL;
    }
    return kName[idx];
}

const char *grok_bots_note(int idx)
{
    if ((idx < 0) || (idx >= GROK_BOTS_COUNT))
    {
        return NULL;
    }
    return kNote[idx];
}

int grok_bots_step(int cur, int delta)
{
    int n = (cur + (delta % GROK_BOTS_COUNT)) % GROK_BOTS_COUNT;

    if (n < 0)
    {
        n += GROK_BOTS_COUNT;
    }
    return n;
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
    name = grok_bots_name(idx);
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

size_t grok_bots_format_note(char *out, size_t cap, int idx)
{
    const char *name;
    const char *note;
    int n;

    if ((out == NULL) || (cap == 0))
    {
        return 0;
    }
    name = grok_bots_name(idx);
    note = grok_bots_note(idx);
    if ((name == NULL) || (note == NULL))
    {
        out[0] = '\0';
        return 0;
    }
    if (note[0] == '\0')
    {
        n = snprintf(out, cap, "%s\n\nWhere we left off:\n(nothing yet)", name);
    }
    else
    {
        n = snprintf(out, cap, "%s\n\nWhere we left off:\n%s", name, note);
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
