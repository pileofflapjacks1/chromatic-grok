/* Host tests for components/grok/grok_keys.c.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_keys.h"

#include <stdio.h>
#include <string.h>

static int s_fail;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            s_fail++;                                                  \
        }                                                              \
    } while (0)

static void test_layout(void)
{
    GrokKeys_t k = { .row = 1, .col = 0, .shift = false };
    char row[GROK_KEYS_ROW_CHARS + 1];
    char one[2];
    char ch;
    int col = -1;

    grok_keys_row(&k, 0, row);
    CHECK(strcmp(row, "1 2 3 4 5 6 7 8 9 0 ") == 0);
    grok_keys_row(&k, 1, row);
    CHECK(strcmp(row, "q w e r t y u i o p ") == 0);
    grok_keys_row(&k, 4, row);
    CHECK(strcmp(row, "CAP SPACE  DEL  SEND") == 0);
    grok_keys_row(&k, 9, row);
    CHECK(strlen(row) == GROK_KEYS_ROW_CHARS);

    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_CHAR);
    CHECK(ch == 'q');
    CHECK(strcmp(grok_keys_label(&k, one, &col), "q") == 0);
    CHECK(col == 0);
    k.col = 9;
    CHECK(strcmp(grok_keys_label(&k, one, &col), "p") == 0);
    CHECK(col == 18);

    k.shift = true;
    grok_keys_row(&k, 1, row);
    CHECK(strcmp(row, "Q W E R T Y U I O P ") == 0);
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_CHAR);
    CHECK(ch == 'P');
    k.row = 0;
    k.col = 0;
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_CHAR);
    CHECK(ch == '!');
}

static void test_every_key_is_printable(void)
{
    char row[GROK_KEYS_ROW_CHARS + 1];
    char one[2];

    for (int shift = 0; shift < 2; shift++)
    {
        for (int r = 0; r < GROK_KEYS_ROWS; r++)
        {
            GrokKeys_t k = { .row = (uint8_t)r, .col = 0, .shift = (shift != 0) };

            grok_keys_row(&k, r, row);
            for (int c = 0; c < GROK_KEYS_COLS; c++)
            {
                char ch = 0;
                int col = -1;
                const char *label;
                GrokKeyKind_t kind;

                k.col = (uint8_t)c;
                kind = grok_keys_selected(&k, &ch);
                label = grok_keys_label(&k, one, &col);
                /* the cursor label matches what the row shows there */
                CHECK((col >= 0) && ((size_t)col + strlen(label) <= GROK_KEYS_ROW_CHARS));
                CHECK(strncmp(row + col, label, strlen(label)) == 0);
                if (kind == GROK_KEY_CHAR)
                {
                    CHECK((ch > 0x20) && (ch < 0x7f));
                }
                else
                {
                    CHECK(r == GROK_KEYS_CHAR_ROWS);
                }
            }
        }
    }
}

static void test_move(void)
{
    GrokKeys_t k = { .row = 1, .col = 0, .shift = false };
    char ch;

    grok_keys_move(&k, -1, 0);
    CHECK((k.row == 1) && (k.col == 9));
    grok_keys_move(&k, 1, 0);
    CHECK(k.col == 0);
    grok_keys_move(&k, 0, -1);
    CHECK(k.row == 0);
    grok_keys_move(&k, 0, -1);
    CHECK(k.row == 4);
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_SHIFT);
    grok_keys_move(&k, 1, 0);
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_SPACE);
    grok_keys_move(&k, 1, 0);
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_DEL);
    grok_keys_move(&k, 1, 0);
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_SEND);
    grok_keys_move(&k, 1, 0);
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_SHIFT);
    grok_keys_move(&k, -1, 0);
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_SEND);
    /* down from the wide row wraps to the digits, under the wide key */
    grok_keys_move(&k, 0, 1);
    CHECK((k.row == 0) && (k.col == 8));
    /* every column of the wide row is one of the four keys */
    for (int c = 0; c < GROK_KEYS_COLS; c++)
    {
        k.row = 4;
        k.col = (uint8_t)c;
        CHECK(grok_keys_selected(&k, &ch) != GROK_KEY_CHAR);
    }
    /* a bad state is repaired, not read out of range */
    k.row = 200;
    k.col = 200;
    CHECK(grok_keys_selected(&k, &ch) == GROK_KEY_SHIFT);
    grok_keys_move(&k, 1, 0);
    CHECK((k.row < GROK_KEYS_ROWS) && (k.col < GROK_KEYS_COLS));
    grok_keys_move(NULL, 1, 1);
}

static void test_edit(void)
{
    char buf[6] = "";

    CHECK(!grok_keys_type(buf, sizeof(buf), ' '));
    CHECK(grok_keys_type(buf, sizeof(buf), 'h'));
    CHECK(grok_keys_type(buf, sizeof(buf), 'i'));
    CHECK(grok_keys_type(buf, sizeof(buf), ' '));
    CHECK(!grok_keys_type(buf, sizeof(buf), ' '));
    CHECK(!grok_keys_type(buf, sizeof(buf), '\n'));
    CHECK(grok_keys_type(buf, sizeof(buf), 'y'));
    CHECK(grok_keys_type(buf, sizeof(buf), 'o'));
    CHECK(!grok_keys_type(buf, sizeof(buf), 'u'));
    CHECK(strcmp(buf, "hi yo") == 0);
    CHECK(grok_keys_backspace(buf));
    CHECK(strcmp(buf, "hi y") == 0);
    buf[0] = '\0';
    CHECK(!grok_keys_backspace(buf));
    CHECK(!grok_keys_backspace(NULL));
    CHECK(!grok_keys_type(NULL, 4, 'a'));
}

static void test_tail(void)
{
    char out[4 * 6];
    char small[4];

    grok_keys_tail("", out, sizeof(out), 5, 4);
    CHECK(strcmp(out, "_\n\n\n") == 0);
    grok_keys_tail("abc", out, sizeof(out), 5, 4);
    CHECK(strcmp(out, "abc_\n\n\n") == 0);
    /* the cursor wraps to its own line when the text fills one */
    grok_keys_tail("abcde", out, sizeof(out), 5, 4);
    CHECK(strcmp(out, "abcde\n_\n\n") == 0);
    grok_keys_tail("abcdefghijklmnopqrst", out, sizeof(out), 5, 4);
    CHECK(strcmp(out, "fghij\nklmno\npqrst\n_") == 0);
    grok_keys_tail(NULL, out, sizeof(out), 5, 4);
    CHECK(strcmp(out, "_\n\n\n") == 0);
    small[0] = 'x';
    grok_keys_tail("abc", small, sizeof(small), 5, 4);
    CHECK(small[0] == '\0');
}

int main(void)
{
    test_layout();
    test_every_key_is_printable();
    test_move();
    test_edit();
    test_tail();
    if (s_fail)
    {
        printf("grok keys tests: %d failed\n", s_fail);
        return 1;
    }
    printf("grok keys tests: ok\n");
    return 0;
}
