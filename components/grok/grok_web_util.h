/* Portable helpers for the phone page (grok_web.c): PIN check with lockout,
 * form/query parsing and question sanitizing. Host-tested.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GROK_WEB_PIN_LEN 6
#define GROK_WEB_MAX_FAILS 5
#define GROK_WEB_LOCK_MS 60000

typedef enum {
    GROK_WEB_PIN_OK = 0,
    GROK_WEB_PIN_BAD,
    GROK_WEB_PIN_LOCKED,
} GrokWebPin_t;

typedef struct {
    uint8_t fails;
    int64_t locked_until_ms;
} GrokWebGuard_t;

/* Six decimal digits from a random word, zero padded. out needs 7 bytes. */
void grok_web_pin_format(uint32_t r, char out[GROK_WEB_PIN_LEN + 1]);

/* Constant-time compare of a 6-digit PIN. Wrong length or NULL is false. */
bool grok_web_pin_equal(const char *want, const char *got);

/* PIN check with lockout: GROK_WEB_MAX_FAILS wrong PINs in a row lock every
 * check for GROK_WEB_LOCK_MS. A right PIN resets the count. While locked,
 * even the right PIN is refused. */
GrokWebPin_t grok_web_guard_check(GrokWebGuard_t *guard, int64_t now_ms, const char *want, const char *got);

/* Seconds left in a lockout, 0 when not locked. */
uint32_t grok_web_guard_wait_s(const GrokWebGuard_t *guard, int64_t now_ms);

/* Value of key in an application/x-www-form-urlencoded body or query string,
 * URL-decoded ('+' is a space, %XX a byte). False if missing, malformed or
 * longer than out_cap - 1. */
bool grok_web_form_get(const char *form, size_t form_len, const char *key, char *out, size_t out_cap);

/* In place: CR/LF/TAB become spaces, other bytes outside printable ASCII are
 * dropped, runs of spaces collapse, ends are trimmed. Returns the length. */
size_t grok_web_sanitize(char *text);
