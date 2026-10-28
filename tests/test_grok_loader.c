/* Host checks for loader RLE frames. Not part of the ESP-IDF build.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_loader.h"

#include <stdio.h>
#include <string.h>

static int g_failed;

static void expect_true(const char *name, int cond)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL %s\n", name);
        g_failed++;
    }
}

static void test_loader(void)
{
    static uint8_t buf[GROK_LOADER_WIDTH * GROK_LOADER_HEIGHT];
    size_t flash;
    uint32_t cycle = 0;
    static const uint16_t expect_ms[GROK_LOADER_FRAME_COUNT] = {
        300, 200, 200, 150, 120, 200, 300, 300, 120, 300
    };

    expect_true("frame count", GROK_LOADER_FRAME_COUNT == 10);
    expect_true("size", (GROK_LOADER_WIDTH == 160) && (GROK_LOADER_HEIGHT == 144));
    expect_true("palette", GROK_LOADER_PALETTE_COUNT == 3);

    for (int i = 0; i < GROK_LOADER_FRAME_COUNT; i++)
    {
        expect_true("decode", grok_loader_decode_indices(i, buf, sizeof(buf)));
        expect_true("duration", grok_loader_frames[i].duration_ms == expect_ms[i]);
        cycle += grok_loader_frames[i].duration_ms;
        for (size_t p = 0; p < sizeof(buf); p++)
        {
            if (buf[p] >= GROK_LOADER_PALETTE_COUNT)
            {
                expect_true("index in range", 0);
                break;
            }
        }
        /* Background dominates: index 0 should be most common. */
        {
            size_t zeros = 0;
            for (size_t p = 0; p < sizeof(buf); p++)
            {
                if (buf[p] == 0)
                {
                    zeros++;
                }
            }
            expect_true("mostly bg", zeros > (sizeof(buf) / 2));
        }
    }
    expect_true("bad frame", !grok_loader_decode_indices(-1, buf, sizeof(buf)));
    expect_true("tiny buf", !grok_loader_decode_indices(0, buf, 16));

    expect_true("timings sum", cycle == 2190u);
    expect_true("frame at 0", grok_loader_frame_at(0, 0) == 0);
    expect_true("frame at 299", grok_loader_frame_at(299, 0) == 0);
    expect_true("frame at 300", grok_loader_frame_at(300, 0) == 1);
    expect_true("frame at 500", grok_loader_frame_at(500, 0) == 2);
    expect_true("frame at 700", grok_loader_frame_at(700, 0) == 3);
    expect_true("frame at 850", grok_loader_frame_at(850, 0) == 4);
    expect_true("frame at 970", grok_loader_frame_at(970, 0) == 5);
    expect_true("frame at 1170", grok_loader_frame_at(1170, 0) == 6);
    expect_true("frame at 1470", grok_loader_frame_at(1470, 0) == 7);
    expect_true("frame at 1770", grok_loader_frame_at(1770, 0) == 8);
    expect_true("frame at 1890", grok_loader_frame_at(1890, 0) == 9);
    expect_true("frame at 2190", grok_loader_frame_at(2190, 0) == 0);
    expect_true("frame loop", grok_loader_frame_at(cycle + 10, 0) == 0);

    flash = grok_loader_flash_bytes();
    expect_true("flash under 20k", flash < 20 * 1024);
    expect_true("flash around 4k", (flash > 3000) && (flash < 6000));
    printf("loader flash payload: %zu bytes\n", flash);
}

int main(void)
{
    test_loader();
    if (g_failed != 0)
    {
        fprintf(stderr, "%d failed\n", g_failed);
        return 1;
    }
    printf("grok_loader ok\n");
    return 0;
}
