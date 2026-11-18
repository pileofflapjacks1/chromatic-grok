/* Portable helpers for the Bluetooth headset spike.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bt_util.h"

#include <stdio.h>

static const int16_t kStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};

static const int8_t kIndexAdj[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void grok_adpcm_reset(GrokAdpcm_t *st)
{
    st->predictor = 0;
    st->index = 0;
}

int16_t grok_adpcm_decode(GrokAdpcm_t *st, uint8_t code)
{
    int32_t step = kStep[st->index];
    int32_t diff = step >> 3;

    code &= 0x0f;
    if (code & 4)
    {
        diff += step;
    }
    if (code & 2)
    {
        diff += step >> 1;
    }
    if (code & 1)
    {
        diff += step >> 2;
    }
    st->predictor = clamp(st->predictor + ((code & 8) ? -diff : diff), -32768, 32767);
    st->index = clamp(st->index + kIndexAdj[code & 7], 0, 88);
    return (int16_t)st->predictor;
}

uint8_t grok_adpcm_encode(GrokAdpcm_t *st, int16_t sample)
{
    int32_t step = kStep[st->index];
    int32_t diff = (int32_t)sample - st->predictor;
    uint8_t code = 0;

    if (diff < 0)
    {
        code = 8;
        diff = -diff;
    }
    if (diff >= step)
    {
        code |= 4;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step)
    {
        code |= 2;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step)
    {
        code |= 1;
    }
    /* Track the decoder exactly so encode and decode never drift. */
    (void)grok_adpcm_decode(st, code);
    return code;
}

bool grok_adpcm_put(GrokAdpcm_t *st, uint8_t *buf, size_t cap_bytes, size_t *pos, int16_t sample)
{
    size_t byte = *pos >> 1;
    uint8_t code;

    if (byte >= cap_bytes)
    {
        return false;
    }
    code = grok_adpcm_encode(st, sample);
    if ((*pos & 1) == 0)
    {
        buf[byte] = code;
    }
    else
    {
        buf[byte] = (uint8_t)(buf[byte] | (code << 4));
    }
    (*pos)++;
    return true;
}

bool grok_adpcm_get(GrokAdpcm_t *st, const uint8_t *buf, size_t len_nibbles, size_t *pos, int16_t *sample)
{
    uint8_t code;

    if (*pos >= len_nibbles)
    {
        return false;
    }
    code = buf[*pos >> 1];
    code = (*pos & 1) ? (uint8_t)(code >> 4) : (uint8_t)(code & 0x0f);
    *sample = grok_adpcm_decode(st, code);
    (*pos)++;
    return true;
}

static int hex_val(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool grok_bt_parse_mac(const char *text, uint8_t out[6])
{
    if ((text == NULL) || (out == NULL))
    {
        return false;
    }
    for (int i = 0; i < 6; i++)
    {
        const char *p = text + (i * 3);
        int hi = hex_val(p[0]);
        int lo = (hi < 0) ? -1 : hex_val(p[1]);

        if ((hi < 0) || (lo < 0))
        {
            return false;
        }
        if (i < 5)
        {
            if ((p[2] != ':') && (p[2] != '-'))
            {
                return false;
            }
        }
        else if (p[2] != '\0')
        {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void grok_bt_format_mac(const uint8_t mac[6], char *out, size_t out_cap)
{
    if ((out == NULL) || (out_cap == 0))
    {
        return;
    }
    snprintf(out, out_cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool grok_bt_cod_is_audio(uint32_t cod)
{
    /* Bits 8-12: major device class. 0x04 = Audio/Video. */
    return ((cod >> 8) & 0x1f) == 0x04;
}
