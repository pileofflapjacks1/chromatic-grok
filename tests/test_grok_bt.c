/* Host tests for grok_bt_util.c (Bluetooth headset spike helpers).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bt_util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_fail;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_fail++;                                                   \
        }                                                               \
    } while (0)

static void test_mac(void)
{
    uint8_t mac[6];
    char text[18];

    CHECK(grok_bt_parse_mac("00:1A:7d:da:71:13", mac));
    CHECK((mac[0] == 0x00) && (mac[1] == 0x1a) && (mac[5] == 0x13));
    grok_bt_format_mac(mac, text, sizeof(text));
    CHECK(strcmp(text, "00:1a:7d:da:71:13") == 0);
    CHECK(grok_bt_parse_mac("00-1a-7d-da-71-13", mac));
    CHECK(!grok_bt_parse_mac("00:1a:7d:da:71", mac));
    CHECK(!grok_bt_parse_mac("00:1a:7d:da:71:13:", mac));
    CHECK(!grok_bt_parse_mac("00:1a:7d:da:71:1", mac));
    CHECK(!grok_bt_parse_mac("0g:1a:7d:da:71:13", mac));
    CHECK(!grok_bt_parse_mac("001a7dda7113", mac));
    CHECK(!grok_bt_parse_mac("", mac));
    CHECK(!grok_bt_parse_mac(NULL, mac));
}

static void test_cod(void)
{
    CHECK(grok_bt_cod_is_audio(0x240404));  /* wearable headset */
    CHECK(grok_bt_cod_is_audio(0x240418));  /* headphones */
    CHECK(!grok_bt_cod_is_audio(0x5a020c)); /* smartphone */
    CHECK(!grok_bt_cod_is_audio(0x3c010c)); /* laptop */
    CHECK(!grok_bt_cod_is_audio(0));
}

/* A 440 Hz tone at 8 kHz survives the 4-bit round trip with a good SNR. */
static void test_adpcm(void)
{
    enum { kN = 8000 };
    static uint8_t buf[kN / 2];
    GrokAdpcm_t enc;
    GrokAdpcm_t dec;
    size_t wpos = 0;
    size_t rpos = 0;
    double sig = 0.0;
    double err = 0.0;
    int16_t out;

    grok_adpcm_reset(&enc);
    for (int i = 0; i < kN; i++)
    {
        int16_t s = (int16_t)(12000.0 * sin(2.0 * M_PI * 440.0 * i / 8000.0));
        CHECK(grok_adpcm_put(&enc, buf, sizeof(buf), &wpos, s));
    }
    CHECK(wpos == kN);
    CHECK(!grok_adpcm_put(&enc, buf, sizeof(buf), &wpos, 0));
    CHECK(wpos == kN);

    grok_adpcm_reset(&dec);
    for (int i = 0; i < kN; i++)
    {
        int16_t s = (int16_t)(12000.0 * sin(2.0 * M_PI * 440.0 * i / 8000.0));
        CHECK(grok_adpcm_get(&dec, buf, wpos, &rpos, &out));
        if (i >= 200)
        {
            sig += (double)s * s;
            err += (double)(s - out) * (s - out);
        }
    }
    CHECK(!grok_adpcm_get(&dec, buf, wpos, &rpos, &out));
    CHECK((10.0 * log10(sig / (err + 1.0))) > 20.0);

    /* Full-scale steps clamp instead of wrapping. */
    grok_adpcm_reset(&enc);
    for (int i = 0; i < 200; i++)
    {
        (void)grok_adpcm_encode(&enc, (i & 16) ? 32767 : -32768);
        CHECK((enc.index >= 0) && (enc.index <= 88));
    }
}

int main(void)
{
    test_mac();
    test_cod();
    test_adpcm();
    if (s_fail != 0)
    {
        printf("test_grok_bt: %d failure(s)\n", s_fail);
        return 1;
    }
    printf("test_grok_bt: ok\n");
    return 0;
}
