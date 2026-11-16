/* On-screen keyboard model. See grok_keys.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_keys.h"

#include <string.h>

static const char kPlain[GROK_KEYS_CHAR_ROWS][GROK_KEYS_COLS + 1] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl'",
    "zxcvbnm,.?",
};

static const char kShift[GROK_KEYS_CHAR_ROWS][GROK_KEYS_COLS + 1] = {
    "!@#$%&*()-",
    "QWERTYUIOP",
    "ASDFGHJKL\"",
    "ZXCVBNM;:/",
};

/* Wide keys: "CAP SPACE  DEL  SEND" */
enum { kWideCount = 4 };
static const char kWideRow[GROK_KEYS_ROW_CHARS + 1] = "CAP SPACE  DEL  SEND";
static const char *const kWideLabel[kWideCount] = { "CAP", "SPACE", "DEL", "SEND" };
static const uint8_t kWideTextCol[kWideCount] = { 0, 4, 11, 16 };
/* The key column a wide key is entered at, so Up lands under it. */
static const uint8_t kWideHome[kWideCount] = { 0, 3, 6, 8 };
static const GrokKeyKind_t kWideKind[kWideCount] = {
    GROK_KEY_SHIFT, GROK_KEY_SPACE, GROK_KEY_DEL, GROK_KEY_SEND,
};

static int wide_index(int col)
{
    if (col <= 1)
    {
        return 0;
    }
    if (col <= 4)
    {
        return 1;
    }
    if (col <= 7)
    {
        return 2;
    }
    return 3;
}

static int wrap(int v, int n)
{
    v %= n;
    return (v < 0) ? (v + n) : v;
}

static bool valid(const GrokKeys_t *k)
{
    return (k != NULL) && (k->row < GROK_KEYS_ROWS) && (k->col < GROK_KEYS_COLS);
}

void grok_keys_move(GrokKeys_t *k, int dx, int dy)
{
    if (k == NULL)
    {
        return;
    }
    if (!valid(k))
    {
        k->row = 1;
        k->col = 0;
    }
    if (dy != 0)
    {
        k->row = (uint8_t)wrap((int)k->row + dy, GROK_KEYS_ROWS);
    }
    if (dx != 0)
    {
        if (k->row == GROK_KEYS_CHAR_ROWS)
        {
            k->col = kWideHome[wrap(wide_index(k->col) + dx, kWideCount)];
        }
        else
        {
            k->col = (uint8_t)wrap((int)k->col + dx, GROK_KEYS_COLS);
        }
    }
}

GrokKeyKind_t grok_keys_selected(const GrokKeys_t *k, char *ch)
{
    if (ch != NULL)
    {
        *ch = '\0';
    }
    if (!valid(k))
    {
        return GROK_KEY_SHIFT;
    }
    if (k->row == GROK_KEYS_CHAR_ROWS)
    {
        return kWideKind[wide_index(k->col)];
    }
    if (ch != NULL)
    {
        *ch = (k->shift ? kShift : kPlain)[k->row][k->col];
    }
    return GROK_KEY_CHAR;
}

const char *grok_keys_label(const GrokKeys_t *k, char one[2], int *text_col)
{
    char ch = '\0';
    int col = 0;
    const char *label;

    if (!valid(k))
    {
        label = kWideLabel[0];
    }
    else if (grok_keys_selected(k, &ch) == GROK_KEY_CHAR)
    {
        one[0] = ch;
        one[1] = '\0';
        label = one;
        col = (int)k->col * 2;
    }
    else
    {
        int idx = wide_index(k->col);

        label = kWideLabel[idx];
        col = kWideTextCol[idx];
    }
    if (text_col != NULL)
    {
        *text_col = col;
    }
    return label;
}

void grok_keys_row(const GrokKeys_t *k, int row, char out[GROK_KEYS_ROW_CHARS + 1])
{
    const char *keys;

    memset(out, ' ', GROK_KEYS_ROW_CHARS);
    out[GROK_KEYS_ROW_CHARS] = '\0';
    if ((row < 0) || (row >= GROK_KEYS_ROWS))
    {
        return;
    }
    if (row == GROK_KEYS_CHAR_ROWS)
    {
        memcpy(out, kWideRow, GROK_KEYS_ROW_CHARS);
        return;
    }
    keys = (((k != NULL) && k->shift) ? kShift : kPlain)[row];
    for (int i = 0; i < GROK_KEYS_COLS; i++)
    {
        out[i * 2] = keys[i];
    }
}

bool grok_keys_type(char *buf, size_t cap, char c)
{
    size_t len;

    if ((buf == NULL) || (cap == 0) || (c < 0x20) || (c > 0x7e))
    {
        return false;
    }
    len = strlen(buf);
    if ((len + 1) >= cap)
    {
        return false;
    }
    if ((c == ' ') && ((len == 0) || (buf[len - 1] == ' ')))
    {
        return false;
    }
    buf[len] = c;
    buf[len + 1] = '\0';
    return true;
}

bool grok_keys_backspace(char *buf)
{
    size_t len;

    if ((buf == NULL) || (buf[0] == '\0'))
    {
        return false;
    }
    len = strlen(buf);
    buf[len - 1] = '\0';
    return true;
}

void grok_keys_tail(const char *buf, char *out, size_t out_cap, size_t cols, size_t lines)
{
    size_t len;
    size_t total;     /* characters to draw, cursor included */
    size_t used_lines;
    size_t first;
    size_t w = 0;

    if ((out == NULL) || (out_cap == 0))
    {
        return;
    }
    out[0] = '\0';
    if ((cols == 0) || (lines == 0) || (out_cap < (lines * (cols + 1))))
    {
        return;
    }
    if (buf == NULL)
    {
        buf = "";
    }
    len = strlen(buf);
    total = len + 1;
    used_lines = (total + cols - 1) / cols;
    first = (used_lines > lines) ? ((used_lines - lines) * cols) : 0;
    for (size_t line = 0; line < lines; line++)
    {
        size_t at = first + (line * cols);

        if (line > 0)
        {
            out[w++] = '\n';
        }
        for (size_t i = 0; (i < cols) && ((at + i) < total); i++)
        {
            out[w++] = ((at + i) < len) ? buf[at + i] : '_';
        }
    }
    out[w] = '\0';
}
