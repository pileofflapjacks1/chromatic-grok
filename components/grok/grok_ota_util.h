/* Portable helpers for "grok update". No ESP-IDF calls, so the host tests
 * in tests/ can build them.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where the image header and app description sit in an app .bin:
 * esp_image_header_t (24) + first esp_image_segment_header_t (8). */
#define GROK_OTA_DESC_OFFSET 32u
#define GROK_OTA_HEAD_MIN (GROK_OTA_DESC_OFFSET + 80u)
#define GROK_OTA_PROJECT "chromatic_grok"

typedef enum {
    GROK_OTA_HEAD_OK = 0,
    GROK_OTA_HEAD_SHORT,       /* fewer than GROK_OTA_HEAD_MIN bytes */
    GROK_OTA_HEAD_BAD_MAGIC,   /* not an ESP app image (0xE9) */
    GROK_OTA_HEAD_BAD_DESC,    /* no esp_app_desc_t magic */
    GROK_OTA_HEAD_WRONG_APP,   /* an image for some other project */
} GrokOtaHead_t;

/* Check the first bytes of a downloaded image. On success copies the app
 * version (NUL terminated, truncated to version_cap) into version. */
GrokOtaHead_t grok_ota_check_head(const uint8_t *buf, size_t len, char *version, size_t version_cap);
const char *grok_ota_head_reason(GrokOtaHead_t r);

/* https:// only, printable, no spaces, fits in cap-1 chars. */
bool grok_ota_url_ok(const char *url, size_t cap);

/* HTTP status codes esp_http_client must follow by hand. */
bool grok_ota_is_redirect(int status);

typedef enum {
    GROK_OTA_BOOT_NORMAL = 0,     /* nothing pending */
    GROK_OTA_BOOT_NOT_TAKEN,      /* pending, but the bootloader ran the old slot */
    GROK_OTA_BOOT_TRIAL,          /* pending, new slot running: count it, run checks */
    GROK_OTA_BOOT_ROLL_BACK,      /* too many unconfirmed boots of the new slot */
} GrokOtaBoot_t;

/* Decide what to do early in boot. boots is the number of earlier boots of
 * the new slot that never confirmed (before this one). */
GrokOtaBoot_t grok_ota_boot_decision(bool pending, bool running_is_new, unsigned boots, unsigned max_boots);
