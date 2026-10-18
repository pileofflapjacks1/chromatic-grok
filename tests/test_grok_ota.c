/* Host tests for components/grok/grok_ota_util.c.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_ota_util.h"

#include <stdio.h>
#include <string.h>

static int s_fail;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            s_fail++;                                                  \
        }                                                              \
    } while (0)

static void make_image(uint8_t *buf, size_t len, const char *project, const char *version)
{
    memset(buf, 0, len);
    buf[0] = 0xE9;
    /* esp_app_desc_t magic 0xABCD5432, little endian, at offset 32 */
    buf[32] = 0x32;
    buf[33] = 0x54;
    buf[34] = 0xCD;
    buf[35] = 0xAB;
    memcpy(buf + 32 + 16, version, strlen(version));
    memcpy(buf + 32 + 48, project, strlen(project));
}

static void test_head(void)
{
    uint8_t img[256];
    char ver[33];

    make_image(img, sizeof(img), "chromatic_grok", "0.15.0-grok");
    CHECK(grok_ota_check_head(img, sizeof(img), ver, sizeof(ver)) == GROK_OTA_HEAD_OK);
    CHECK(strcmp(ver, "0.15.0-grok") == 0);
    CHECK(grok_ota_check_head(img, sizeof(img), ver, 5) == GROK_OTA_HEAD_OK);
    CHECK(strcmp(ver, "0.15") == 0);

    CHECK(grok_ota_check_head(img, GROK_OTA_HEAD_MIN - 1, ver, sizeof(ver)) == GROK_OTA_HEAD_SHORT);
    CHECK(ver[0] == '\0');
    CHECK(grok_ota_check_head(NULL, 0, NULL, 0) == GROK_OTA_HEAD_SHORT);

    img[0] = 0x7F; /* e.g. an HTML error page or an ELF */
    CHECK(grok_ota_check_head(img, sizeof(img), ver, sizeof(ver)) == GROK_OTA_HEAD_BAD_MAGIC);

    make_image(img, sizeof(img), "chromatic_grok", "1");
    img[35] = 0;
    CHECK(grok_ota_check_head(img, sizeof(img), ver, sizeof(ver)) == GROK_OTA_HEAD_BAD_DESC);

    make_image(img, sizeof(img), "hello_world", "1");
    CHECK(grok_ota_check_head(img, sizeof(img), ver, sizeof(ver)) == GROK_OTA_HEAD_WRONG_APP);
    make_image(img, sizeof(img), "chromatic_grok_x", "1");
    CHECK(grok_ota_check_head(img, sizeof(img), ver, sizeof(ver)) == GROK_OTA_HEAD_WRONG_APP);
    CHECK(strcmp(grok_ota_head_reason(GROK_OTA_HEAD_WRONG_APP), "not a chromatic_grok image") == 0);
}

static void test_url(void)
{
    char longurl[260];

    CHECK(grok_ota_url_ok("https://github.com/a/b/releases/latest/download/chromatic_grok.signed.bin", 201));
    CHECK(grok_ota_url_ok("https://192.168.1.5:8443/fw.bin", 201));
    CHECK(!grok_ota_url_ok("http://github.com/x.bin", 201));
    CHECK(!grok_ota_url_ok("https://", 201));
    CHECK(!grok_ota_url_ok("https:///x.bin", 201));
    CHECK(!grok_ota_url_ok("https://a b/x.bin", 201));
    CHECK(!grok_ota_url_ok("ftp://a/x.bin", 201));
    CHECK(!grok_ota_url_ok(NULL, 201));
    memset(longurl, 'a', sizeof(longurl));
    memcpy(longurl, "https://", 8);
    longurl[200] = '\0';
    CHECK(grok_ota_url_ok(longurl, 201));
    longurl[200] = 'a';
    longurl[201] = '\0';
    CHECK(!grok_ota_url_ok(longurl, 201));
}

static void test_redirect(void)
{
    CHECK(grok_ota_is_redirect(301));
    CHECK(grok_ota_is_redirect(302));
    CHECK(grok_ota_is_redirect(303));
    CHECK(grok_ota_is_redirect(307));
    CHECK(grok_ota_is_redirect(308));
    CHECK(!grok_ota_is_redirect(200));
    CHECK(!grok_ota_is_redirect(304));
    CHECK(!grok_ota_is_redirect(404));
}

static void test_boot(void)
{
    CHECK(grok_ota_boot_decision(false, true, 5, 2) == GROK_OTA_BOOT_NORMAL);
    CHECK(grok_ota_boot_decision(false, false, 0, 2) == GROK_OTA_BOOT_NORMAL);
    /* bootloader ran the old slot (new image failed its load checks) */
    CHECK(grok_ota_boot_decision(true, false, 0, 2) == GROK_OTA_BOOT_NOT_TAKEN);
    /* first and second boot of the new slot are trials */
    CHECK(grok_ota_boot_decision(true, true, 0, 2) == GROK_OTA_BOOT_TRIAL);
    CHECK(grok_ota_boot_decision(true, true, 1, 2) == GROK_OTA_BOOT_TRIAL);
    /* third boot without a confirmation rolls back */
    CHECK(grok_ota_boot_decision(true, true, 2, 2) == GROK_OTA_BOOT_ROLL_BACK);
    CHECK(grok_ota_boot_decision(true, true, 200, 2) == GROK_OTA_BOOT_ROLL_BACK);
}

int main(void)
{
    test_head();
    test_url();
    test_redirect();
    test_boot();
    if (s_fail)
    {
        printf("grok ota tests: %d failed\n", s_fail);
        return 1;
    }
    printf("grok ota tests: ok\n");
    return 0;
}
