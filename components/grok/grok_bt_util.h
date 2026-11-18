/* Portable helpers for the Bluetooth headset spike (grok_bt.c). No ESP-IDF
 * headers, so tests/test_grok_bt.c can build them on the host.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* IMA ADPCM, 4 bits per sample. 5 s of 8 kHz headset mic fits in 20 KB
 * instead of 80 KB of 16-bit PCM. Two samples per byte, low nibble first. */
typedef struct {
    int32_t predictor;
    int32_t index;
} GrokAdpcm_t;

void grok_adpcm_reset(GrokAdpcm_t *st);
uint8_t grok_adpcm_encode(GrokAdpcm_t *st, int16_t sample);
int16_t grok_adpcm_decode(GrokAdpcm_t *st, uint8_t code);

/* Append one sample at nibble position *pos (buf holds cap_bytes). False
 * once the buffer is full. */
bool grok_adpcm_put(GrokAdpcm_t *st, uint8_t *buf, size_t cap_bytes, size_t *pos, int16_t sample);
/* Decode the sample at nibble position *pos; false when pos >= len_nibbles. */
bool grok_adpcm_get(GrokAdpcm_t *st, const uint8_t *buf, size_t len_nibbles, size_t *pos, int16_t *sample);

/* "aa:bb:cc:dd:ee:ff" (':' or '-', any case). False on anything else. */
bool grok_bt_parse_mac(const char *text, uint8_t out[6]);
/* out_cap >= 18. */
void grok_bt_format_mac(const uint8_t mac[6], char *out, size_t out_cap);

/* Class of Device: major class Audio/Video (headset, headphones, speaker,
 * car kit). Phones and computers are not. */
bool grok_bt_cod_is_audio(uint32_t cod);
