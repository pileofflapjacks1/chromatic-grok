/* Static Grok Bot / team list for the on-device menu (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GROK_BOTS_COUNT 4
/* Bots + "Ask Grok" + "Phone typing". */
#define GROK_BOTS_ASK_IDX GROK_BOTS_COUNT
#define GROK_BOTS_PHONE_IDX (GROK_BOTS_COUNT + 1)
#define GROK_BOTS_MENU_COUNT (GROK_BOTS_COUNT + 2)

/* On-screen name length budget: same row prefix as the question picker. */
#define GROK_BOTS_ROW_PREFIX 3
#define GROK_BOTS_NAME_MAX 17

/* Per-bot last prompt / reply snippet caps (NVS + note screen). Small on
 * purpose: one packed NVS string per bot, 24 KB NVS shared with Wi-Fi keys. */
#define GROK_BOTS_LAST_PROMPT_MAX 80
#define GROK_BOTS_LAST_REPLY_MAX 40
#define GROK_BOTS_LAST_PACK_MAX (GROK_BOTS_LAST_PROMPT_MAX + 1 + GROK_BOTS_LAST_REPLY_MAX)

/* Built-in / overridden bot name (flash or RAM). NULL if out of range. */
const char *grok_bots_name(int idx);

/* Override the display name for a bot slot (clipped). Empty restores default. */
void grok_bots_set_name(int idx, const char *name);

/* Built-in default name (never overridden). NULL if out of range. */
const char *grok_bots_default_name(int idx);

/* Which bot asks land on (0..GROK_BOTS_COUNT-1). Phone typing / serial use it. */
void grok_bots_select(int idx);
int grok_bots_selected(void);

/* True while "Ask Grok" fast mode is selected (proxy/direct, not webhook). */
void grok_bots_set_fast(bool on);
bool grok_bots_fast(void);

/* Move selection by delta; wrap within 0..menu_count-1. */
int grok_bots_menu_step(int cur, int delta, int menu_count);

/* Legacy wrap within bots only (serial helpers / older tests). */
int grok_bots_step(int cur, int delta);

/* One list row: '>' when selected, 1-based index, space, then the name. */
size_t grok_bots_format_row(char *out, size_t cap, int idx, bool selected,
                            size_t cols);

/* Clip src into dst to at most max_len printable ASCII chars (NUL-terminated). */
void grok_bots_clip(char *dst, size_t dst_cap, const char *src, size_t max_len);

/* Pack prompt + reply into one NVS string ("prompt\nreply"). Truncates each. */
size_t grok_bots_pack_last(char *out, size_t cap, const char *prompt,
                           const char *reply);

/* Unpack a packed last string. Missing parts become "". */
void grok_bots_unpack_last(const char *packed, char *prompt, size_t prompt_cap,
                           char *reply, size_t reply_cap);

/* Body text for the "where we left off" screen. Empty prompt -> (nothing yet). */
size_t grok_bots_format_note(char *out, size_t cap, int idx,
                             const char *prompt, const char *reply);
