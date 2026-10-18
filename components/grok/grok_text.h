/* Portable text helpers for the Chromatic Grok link.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

/* Overlay wrap: 20 columns of 8 px unscii_8 on the 160 px screen; 52 lines = 4 pages of 13. */
#define GROK_TEXT_COLS 20
#define GROK_TEXT_LINES 52
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

/* True if the character at buf[pos] is part of a secret argument
 * (grok key set / grok token set / wifi set <ssid> password) and must not be
 * echoed. buf[0..pos) is the text typed so far. */
bool grok_text_secret_at(const char *buf, size_t pos);
