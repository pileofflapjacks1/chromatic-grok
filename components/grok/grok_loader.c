/* Loading-screen frame helpers (host-tested decode; device paints via LVGL).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_loader.h"

#include <string.h>

bool grok_loader_decode_indices(int frame, uint8_t *out, size_t out_cap)
{
    const GrokLoaderFrame *fr;
    size_t need = (size_t)GROK_LOADER_WIDTH * (size_t)GROK_LOADER_HEIGHT;
    size_t written = 0;
    size_t i;

    if ((out == NULL) || (out_cap < need))
    {
        return false;
    }
    if ((frame < 0) || (frame >= GROK_LOADER_FRAME_COUNT))
    {
        return false;
    }
    fr = &grok_loader_frames[frame];
    if ((fr->rle == NULL) || ((fr->rle_len % 2) != 0))
    {
        return false;
    }
    for (i = 0; i + 1 < fr->rle_len; i += 2)
    {
        uint8_t count = fr->rle[i];
        uint8_t index = fr->rle[i + 1];

        if (index >= GROK_LOADER_PALETTE_COUNT)
        {
            return false;
        }
        if ((written + count) > need)
        {
            return false;
        }
        memset(out + written, index, count);
        written += count;
    }
    return written == need;
}

int grok_loader_frame_at(uint32_t now_ms, uint32_t start_ms)
{
    uint32_t elapsed;
    uint32_t cycle = 0;
    uint32_t t;
    int i;

    for (i = 0; i < GROK_LOADER_FRAME_COUNT; i++)
    {
        cycle += grok_loader_frames[i].duration_ms;
    }
    if (cycle == 0)
    {
        return 0;
    }
    elapsed = now_ms - start_ms;
    t = elapsed % cycle;
    for (i = 0; i < GROK_LOADER_FRAME_COUNT; i++)
    {
        if (t < grok_loader_frames[i].duration_ms)
        {
            return i;
        }
        t -= grok_loader_frames[i].duration_ms;
    }
    return 0;
}

size_t grok_loader_flash_bytes(void)
{
    size_t n = (size_t)GROK_LOADER_PALETTE_COUNT * sizeof(uint16_t);
    int i;

    for (i = 0; i < GROK_LOADER_FRAME_COUNT; i++)
    {
        n += grok_loader_frames[i].rle_len;
    }
    return n;
}
