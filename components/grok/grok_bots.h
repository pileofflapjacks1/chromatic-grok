/* Static Grok Bot / team list for the on-device menu (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define GROK_BOTS_COUNT 4

/* On-screen name length budget: same row prefix as the question picker. */
#define GROK_BOTS_ROW_PREFIX 3
#define GROK_BOTS_NAME_MAX 17

/* "Where we left off" note. Empty string means nothing saved yet. */
#define GROK_BOTS_NOTE_MAX 160

/* Built-in bot/team name (flash). NULL if out of range. */
const char *grok_bots_name(int idx);

/* Per-bot note string (flash placeholder for now; empty by default). */
const char *grok_bots_note(int idx);

/* Move selection by delta; wrap within 0..GROK_BOTS_COUNT-1. */
int grok_bots_step(int cur, int delta);

/* One list row like the question picker: '>' when selected, 1-based index,
 * space, then the name clipped to cols. Returns the row length. */
size_t grok_bots_format_row(char *out, size_t cap, int idx, bool selected,
                            size_t cols);

/* Body text for the "where we left off" screen. Writes into out. */
size_t grok_bots_format_note(char *out, size_t cap, int idx);
