/* Portable text helpers for the Chromatic Grok link.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#define GROK_TEXT_COLS 32
#define GROK_TEXT_LINES 40
#define GROK_REPLY_MAX 640
#define GROK_PROMPT_MAX 200

/* Map the string onto printable ASCII in place. Newlines stay. */
void grok_text_fold_ascii(char *text);

/* Write a JSON string body (no surrounding quotes). Returns false if it does not fit. */
bool grok_text_json_escape(char *dst, size_t dst_cap, const char *src, size_t *written);

/* Copy the JSON string value for "key" into out. Handles common escapes. */
bool grok_text_json_get_string(const char *json, const char *key, char *out, size_t out_cap);

/*
 * Word-wrap text into caller storage. Each line is line_stride bytes and is NUL terminated.
 * cols must be less than line_stride. Returns the line count.
 * If the text does not fit, the last line ends with "...".
 */
size_t grok_text_wrap(const char *text, char *storage, size_t line_stride,
                      size_t max_lines, size_t cols);

/* Text of the first output_text block in an xAI/OpenAI Responses API body. */
bool grok_text_responses_text(const char *json, char *out, size_t out_cap);

/* The error string ("error" string, else "message") from an API error body. */
bool grok_text_responses_error(const char *json, char *out, size_t out_cap);

/* Copy in to out, replacing secret and anything shaped like "xai-..." with
 * "[redacted]". Returns the output length. */
size_t grok_text_redact(const char *in, const char *secret, char *out, size_t out_cap);
