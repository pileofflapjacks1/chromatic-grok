/* Preset question picker: portable helpers (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_pick.h"

#include <string.h>

/* const: the pointer table and the strings stay in flash (.rodata). */
static const char *const kBuiltin[GROK_PICK_COUNT] = {
    "Tell me a fun fact.",
    "Give me a one-line joke.",
    "Give me a short riddle. Answer at the end.",
    "Suggest a quick game I can play right now.",
    "What is a good word of the day? Define it.",
    "Explain one science idea in simple words.",
    "Give me one tip for a retro handheld game.",
    "Motivate me in one sentence.",
};

/* Short picker labels, same order as kBuiltin. Each fits one picker row. */
static const char *const kLabel[GROK_PICK_COUNT] = {
    "Fun fact",
    "One-line joke",
    "Short riddle",
    "Quick game idea",
    "Word of the day",
    "Science, simply",
    "Retro game tip",
    "Motivate me",
};

const char *grok_pick_builtin(int idx)
{
    if ((idx < 0) || (idx >= GROK_PICK_COUNT))
    {
        return NULL;
    }
    return kBuiltin[idx];
}

const char *grok_pick_label(int idx)
{
    if ((idx < 0) || (idx >= GROK_PICK_COUNT))
    {
        return NULL;
    }
    return kLabel[idx];
}

int grok_pick_step(int cur, int delta)
{
    int n = (cur + (delta % GROK_PICK_COUNT)) % GROK_PICK_COUNT;

    if (n < 0)
    {
        n += GROK_PICK_COUNT;
    }
    return n;
}

bool grok_pick_parse_slot(const char *text, int *idx)
{
    if ((text == NULL) || (idx == NULL))
    {
        return false;
    }
    if ((text[0] < '1') || (text[0] > ('0' + GROK_PICK_COUNT)) || (text[1] != '\0'))
    {
        return false;
    }
    *idx = text[0] - '1';
    return true;
}

size_t grok_pick_format_row(char *out, size_t cap, int idx, const char *text,
                            bool selected, bool custom, size_t cols)
{
    size_t n = 0;
    size_t limit;

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
    out[n++] = custom ? '*' : ' ';
    if (text != NULL)
    {
        for (const char *p = text; (*p != '\0') && (n < limit); p++)
        {
            char ch = *p;
            out[n++] = ((ch >= 0x20) && (ch <= 0x7e)) ? ch : ' ';
        }
    }
    out[n] = '\0';
    return n;
}
