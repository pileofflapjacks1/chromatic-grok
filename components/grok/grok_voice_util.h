/* Laptop-voice serial framing and caption chunking (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "grok_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
/* Worst folded reply is 640 newlines, which is 640 VOICE_NL lines.
 * The next non-terminator after this many in-frame lines closes the frame
 * without running that line. There is no millisecond timer. */
#define GROK_VOICE_FRAME_MAX_LINES 700

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
    bool fail;       /* discard payload until END/ABORT/budget */
    bool skip_rest;  /* byte feeder: ignore this line after an overlong chunk */
    uint16_t frame_lines;
} GrokVoiceParser_t;

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
