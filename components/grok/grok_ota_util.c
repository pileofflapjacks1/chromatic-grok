/* Portable helpers for "grok update". See grok_ota_util.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_ota_util.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

GrokOtaHead_t grok_ota_check_head(const uint8_t *buf, size_t len, char *version, size_t version_cap)
{
    /* esp_app_desc_t: magic(4) secure_version(4) reserv1(8) version[32]
     * project_name[32] ... */
    const uint8_t *desc;
    size_t n;

    if ((version != NULL) && (version_cap > 0))
    {
        version[0] = '\0';
    }
    if ((buf == NULL) || (len < GROK_OTA_HEAD_MIN))
    {
        return GROK_OTA_HEAD_SHORT;
    }
    if (buf[0] != 0xE9)
    {
        return GROK_OTA_HEAD_BAD_MAGIC;
    }
    desc = buf + GROK_OTA_DESC_OFFSET;
    if (rd32(desc) != 0xABCD5432u)
    {
        return GROK_OTA_HEAD_BAD_DESC;
    }
    if (strncmp((const char *)desc + 48, GROK_OTA_PROJECT, 32) != 0)
    {
        return GROK_OTA_HEAD_WRONG_APP;
    }
    if ((version != NULL) && (version_cap > 0))
    {
        const char *v = (const char *)desc + 16;

        for (n = 0; (n < 32) && (n + 1 < version_cap) && (v[n] != '\0'); n++)
        {
            version[n] = ((v[n] >= 0x20) && (v[n] < 0x7f)) ? v[n] : '?';
        }
        version[n] = '\0';
    }
    return GROK_OTA_HEAD_OK;
}

const char *grok_ota_head_reason(GrokOtaHead_t r)
{
    switch (r)
    {
    case GROK_OTA_HEAD_OK:
        return "ok";
    case GROK_OTA_HEAD_SHORT:
        return "file too short";
    case GROK_OTA_HEAD_BAD_MAGIC:
        return "not an ESP32 app image";
    case GROK_OTA_HEAD_BAD_DESC:
        return "no app description";
    case GROK_OTA_HEAD_WRONG_APP:
        return "not a chromatic_grok image";
    default:
        return "unknown";
    }
}

bool grok_ota_url_ok(const char *url, size_t cap)
{
    size_t n;

    if ((url == NULL) || (strncmp(url, "https://", 8) != 0))
    {
        return false;
    }
    n = strlen(url);
    if ((n <= 8) || (n + 1 > cap) || (url[8] == '/'))
    {
        return false;
    }
    for (size_t i = 0; i < n; i++)
    {
        if ((url[i] <= 0x20) || (url[i] >= 0x7f))
        {
            return false;
        }
    }
    return true;
}

bool grok_ota_is_redirect(int status)
{
    return (status == 301) || (status == 302) || (status == 303) || (status == 307) || (status == 308);
}

GrokOtaBoot_t grok_ota_boot_decision(bool pending, bool running_is_new, unsigned boots, unsigned max_boots)
{
    if (!pending)
    {
        return GROK_OTA_BOOT_NORMAL;
    }
    if (!running_is_new)
    {
        return GROK_OTA_BOOT_NOT_TAKEN;
    }
    if (boots >= max_boots)
    {
        return GROK_OTA_BOOT_ROLL_BACK;
    }
    return GROK_OTA_BOOT_TRIAL;
}
