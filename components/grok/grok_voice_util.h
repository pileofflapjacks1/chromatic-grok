/* Laptop-voice serial framing and caption chunking (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "grok_text.h"

#include <stdbool.h>
#include <stddef.h>

/* Framed markers (whole line, case-sensitive). */
#define GROK_VOICE_BEGIN "VOICE_BEGIN"
#define GROK_VOICE_END "VOICE_END"
#define GROK_VOICE_ABORT "VOICE_ABORT"
#define GROK_VOICE_NL "VOICE_NL"
#define GROK_VOICE_DATA_PREFIX "VOICE_DATA "
#define GROK_VOICE_BOT_PREFIX "VOICE_BOT "
#define GROK_VOICE_READY "VOICE_READY"

/* Console line cap for this protocol. A VOICE_DATA line of CHUNK_MAX bytes
 * is exactly LINE_MAX characters ("VOICE_DATA " + 3 digits + space + 225). */
#define GROK_VOICE_LINE_MAX 240
#define GROK_VOICE_CHUNK_MAX 225
#define GROK_VOICE_BODY_MAX GROK_REPLY_MAX

typedef enum {
    GROK_VOICE_EVT_NONE = 0,
    GROK_VOICE_EVT_LINE,       /* not in a frame; caller may dispatch */
    GROK_VOICE_EVT_BEGIN,      /* VOICE_BEGIN; body reset; in frame */
    GROK_VOICE_EVT_BODY_LINE,  /* literal payload or newline appended */
    GROK_VOICE_EVT_END,        /* clean VOICE_END; body is the reply */
    GROK_VOICE_EVT_HOLD,       /* in frame; line consumed; do not dispatch */
    GROK_VOICE_EVT_DROPPED,    /* frame closed uncleanly; body cleared; consumed */
    GROK_VOICE_EVT_OVERFLOW,   /* byte feeder dropped an overlong line outside a frame */
} GrokVoiceEvt_t;

typedef struct GrokVoiceParser {
    char line[GROK_VOICE_LINE_MAX + 1];
    size_t line_len;
    char body[GROK_VOICE_BODY_MAX + 1];
    size_t body_len;
    bool in_frame;
    bool fail;       /* discard payload until VOICE_END or VOICE_ABORT */
    bool skip_rest;  /* byte feeder: ignore this line after an overlong chunk */
} GrokVoiceParser_t;

/* True when grok_voice_handle_line will return false for a non-verb line,
 * which is when grok_console.c calls esp_console_run. A voice verb on this
 * event is still command dispatch. In-frame lines are never true. */
static inline bool grok_voice_reaches_command(GrokVoiceEvt_t ev)
{
    return ev == GROK_VOICE_EVT_LINE;
}

void grok_voice_parser_init(GrokVoiceParser_t *p);

/* Feed one byte from the serial stream. Returns an event; line_out/body_out
 * point into the parser when the event carries text. */
GrokVoiceEvt_t grok_voice_parser_feed(GrokVoiceParser_t *p, char ch,
                                      const char **line_out);

/* Feed a complete NUL-terminated line (no newline). Same events as feed. */
GrokVoiceEvt_t grok_voice_parser_feed_line(GrokVoiceParser_t *p, const char *line);

/* True if line is a voice console verb: "voice", "voice listening", etc. */
bool grok_voice_is_cmd_line(const char *line);

/* Caption chunking: split folded ASCII into phrases that fit the screen.
 * Prefers punctuation (.!?), else wraps at cols. Writes up to max_chunks
 * NUL-terminated strings into storage (each chunk_stride bytes). Returns
 * the number of chunks written. */
size_t grok_voice_chunk_captions(const char *text, char *storage,
                                 size_t chunk_stride, size_t max_chunks,
                                 size_t cols);

/* Format "VOICE_BOT <name>\n" into out. Returns bytes written (no NUL count
 * requirement beyond snprintf). */
size_t grok_voice_format_bot_line(char *out, size_t out_cap, const char *name);

/* Voice screen layout (160x144, unscii_8 is 8x8). The caption panel has a
 * border and padding, so one page is GROK_VOICE_COLS x GROK_VOICE_ROWS, not
 * the 20 x 13 of the plain reply page. The firmware draws from these numbers
 * and tests/test_grok_voice.c checks that they fit. */
#define GROK_VOICE_COLS 18
#define GROK_VOICE_ROWS 11
#define GROK_VOICE_SCREEN_W 160
#define GROK_VOICE_SCREEN_H 144
#define GROK_VOICE_GLYPH 8        /* glyph width; also 8 px tall, drawn on rows 1-8 of the cell */
#define GROK_VOICE_PITCH 9        /* unscii_8 line height: one text row, zero extra line space */
#define GROK_VOICE_BAR_H 14        /* header bar: y 0..13 */
#define GROK_VOICE_PANEL_X 3
#define GROK_VOICE_PANEL_Y 17
#define GROK_VOICE_PANEL_W 154
#define GROK_VOICE_PANEL_H 108     /* border rows: y 17 and y 124 */
#define GROK_VOICE_PANEL_R 6
#define GROK_VOICE_TEXT_X 8
#define GROK_VOICE_TEXT_Y 21
#define GROK_VOICE_FOOTER_Y 128    /* footer bar: y 128..143 */
#define GROK_VOICE_FOOTER_H 16
#define GROK_VOICE_DOT_Y 100       /* loader art below this row is the three dots */
#define GROK_VOICE_HEAD_X 4        /* name and status keep 4 px from each screen edge */
#define GROK_VOICE_HEAD_COLS 19    /* (160 - 2 * 4) / 8 columns in the header bar */

_Static_assert((GROK_VOICE_TEXT_X + GROK_VOICE_COLS * GROK_VOICE_GLYPH + 4) <=
               (GROK_VOICE_PANEL_X + GROK_VOICE_PANEL_W - 1),
               "voice caption columns overrun the panel border");
_Static_assert((GROK_VOICE_TEXT_Y + (GROK_VOICE_ROWS - 1) * GROK_VOICE_PITCH +
                GROK_VOICE_PITCH + 3) <= (GROK_VOICE_PANEL_Y + GROK_VOICE_PANEL_H - 1),
               "voice caption rows overrun the panel border");
_Static_assert((GROK_VOICE_PANEL_Y + GROK_VOICE_PANEL_H) < GROK_VOICE_FOOTER_Y,
               "voice panel touches the footer bar");
_Static_assert((GROK_VOICE_FOOTER_Y + GROK_VOICE_FOOTER_H) == GROK_VOICE_SCREEN_H,
               "voice footer bar must end at the bottom of the screen");
_Static_assert(GROK_VOICE_PANEL_Y > GROK_VOICE_BAR_H, "voice panel overlaps the header bar");

typedef enum {
    GROK_VOICE_STATUS_LISTENING = 0,
    GROK_VOICE_STATUS_THINKING,
    GROK_VOICE_STATUS_REPLY,
} GrokVoiceStatus_t;

/* "Listening", "Thinking" or "Reply" (anything else reads as "Reply"). */
const char *grok_voice_status_text(GrokVoiceStatus_t status);

/* Header name that fits max_cols columns. A longer name is cut and ends in
 * one '.'. A NULL or empty name becomes "?". Returns bytes written. */
size_t grok_voice_format_title(char *out, size_t out_cap, const char *name, size_t max_cols);

/* Name columns left in the header bar once the status word is placed, with
 * one blank column between them. */
size_t grok_voice_title_cols(GrokVoiceStatus_t status);

/* "2/4" for 0-based page of pages. pages 0 reads as 1. Returns bytes written. */
size_t grok_voice_format_page(char *out, size_t out_cap, size_t page, size_t pages);
