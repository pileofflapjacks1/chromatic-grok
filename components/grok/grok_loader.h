/* Loading-screen frame helpers (host-tested decode; device paints via LVGL).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "grok_loader_frames.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Expand one RLE frame into WIDTH*HEIGHT palette indices. Returns false if
 * the stream is truncated or overshoots the pixel count. */
bool grok_loader_decode_indices(int frame, uint8_t *out, size_t out_cap);

/* Advance the animation clock. Returns the frame index for now_ms given a
 * loop start at start_ms. */
int grok_loader_frame_at(uint32_t now_ms, uint32_t start_ms);

/* Sum of every frame's duration_ms — one full animation cycle. */
uint32_t grok_loader_cycle_ms(void);

/* Total bytes of RLE payloads + RGB565 palette (for flash-size checks). */
size_t grok_loader_flash_bytes(void);
