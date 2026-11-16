/* On-screen keyboard for the Grok app: portable model (host-tested).
 * The screen side is in grok_ui.c.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Four rows of ten character keys, then one row of four wide keys. */
#define GROK_KEYS_CHAR_ROWS 4
#define GROK_KEYS_ROWS 5
#define GROK_KEYS_COLS 10
/* A row is drawn as GROK_KEYS_ROW_CHARS characters (two per key). */
#define GROK_KEYS_ROW_CHARS 20

typedef struct {
    uint8_t row;   /* 0..GROK_KEYS_ROWS-1 */
    uint8_t col;   /* 0..GROK_KEYS_COLS-1 */
    bool shift;    /* capitals and the second symbol set */
} GrokKeys_t;

typedef enum {
    GROK_KEY_CHAR = 0,
    GROK_KEY_SHIFT,
    GROK_KEY_SPACE,
    GROK_KEY_DEL,
    GROK_KEY_SEND,
} GrokKeyKind_t;

/* Move the selection one key. Both directions wrap. On the wide-key row
 * left/right step from one wide key to the next. */
void grok_keys_move(GrokKeys_t *k, int dx, int dy);

/* The selected key. For GROK_KEY_CHAR, *ch is the character it types. */
GrokKeyKind_t grok_keys_selected(const GrokKeys_t *k, char *ch);

/* Text of the selected key as drawn (one character, or "CAP", "SPACE",
 * "DEL", "SEND") and the text column (0..19) where it starts in its row.
 * one is scratch for the one-character case. */
const char *grok_keys_label(const GrokKeys_t *k, char one[2], int *text_col);

/* Row `row` as drawn: GROK_KEYS_ROW_CHARS characters, NUL terminated. */
void grok_keys_row(const GrokKeys_t *k, int row, char out[GROK_KEYS_ROW_CHARS + 1]);

/* Append c to the NUL-terminated text in buf (cap bytes). A space is not
 * added at the start or after another space. False if nothing was added. */
bool grok_keys_type(char *buf, size_t cap, char c);

/* Remove the last character. False if buf was empty. */
bool grok_keys_backspace(char *buf);

/* The end of the text, with a '_' cursor after it, hard-wrapped at cols:
 * exactly `lines` lines joined by '\n' (short ones are empty), showing
 * the last `lines` lines. out needs lines * (cols + 1) bytes. */
void grok_keys_tail(const char *buf, char *out, size_t out_cap, size_t cols, size_t lines);
