/* Preset question picker: portable helpers (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define GROK_PICK_COUNT 8

/* Picker row prefix ('>', slot number, '*'), so a label gets the rest of
 * a GROK_TEXT_COLS (20) row: 17 characters. */
#define GROK_PICK_ROW_PREFIX 3
#define GROK_PICK_LABEL_MAX 17

/* Built-in question idx (0-based), stored in flash. NULL if out of range. */
const char *grok_pick_builtin(int idx);

/* Short on-screen label for built-in idx (at most GROK_PICK_LABEL_MAX
 * characters). Display only: the full question is what gets asked. */
const char *grok_pick_label(int idx);

/* Move the selection by delta and wrap within 0..GROK_PICK_COUNT-1. */
int grok_pick_step(int cur, int delta);

/* Parse a 1-based slot number "1".."8" into a 0-based index. */
bool grok_pick_parse_slot(const char *text, int *idx);

/* One list row, at most cols characters: '>' when selected, the slot
 * number, '*' when the slot holds a custom question, then the question
 * clipped to fit. Returns the row length. */
size_t grok_pick_format_row(char *out, size_t cap, int idx, const char *text,
                            bool selected, bool custom, size_t cols);
