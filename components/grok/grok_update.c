/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * "grok update": wireless firmware update into the inactive OTA slot.
 *
 * Signature: ESP-IDF "signed apps without secure boot", ECDSA P-256 (the
 * Secure Boot V1 signature block, espsecure.py sign_data --version 1). The
 * public key is embedded in this app at build time. esp_ota_end() refuses
 * an image whose signature does not verify, so the boot partition is never
 * switched to it. The stock bootloader is not changed and no eFuse is
 * written. See README "Wireless updates".
 *
 * Rollback: the stock bootloader is built without
 * CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, so rollback is done here, in the
 * app, with a pending flag in NVS namespace "grokupd":
 *   - after a verified download: pend=1, new=<slot>, prev=<slot>, boots=0,
 *     then the boot partition is switched and the chip restarts;
 *   - every boot of the new slot while pending counts a trial boot; after
 *     GROK_UPD_MAX_BOOTS unconfirmed boots it switches back to prev;
 *   - the trial is confirmed once Wi-Fi has an IP and the console task is
 *     running; if that has not happened within GROK_UPD_HEALTH_S seconds
 *     it switches back to prev and restarts.
 * An image that crashes before app_main runs, or hangs with no watchdog
 * reset, is not caught by this. See README for the exact guarantees.
 */
#include "grok.h"
#include "grok_internal.h"
#include "grok_ota_util.h"

#include "sdkconfig.h"

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !CONFIG_SECURE_SIGNED_ON_UPDATE
#error "grok update needs CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT (see sdkconfig.defaults)"
#endif

#define GROK_UPD_NS "grokupd"
#define GROK_UPD_MAX_BOOTS 2
#define GROK_UPD_HEALTH_S 90
#define GROK_UPD_CHUNK 2048
#define GROK_UPD_LABEL_MAX 16
#define GROK_UPD_LAST_MAX 96

/* Key id = SHA-256 of the DER SubjectPublicKeyInfo, the same number as
 * `openssl pkey -pubin -in keys/joe_signing.pub.pem -outform DER | sha256sum`.
 * First 8 bytes of Joe's release key (keys/joe_signing.pub.pem). */
static const uint8_t kReleaseKeyId[8] = {0x13, 0xb8, 0x63, 0x39, 0xc3, 0x62, 0x56, 0xda};
/* DER prefix of an uncompressed P-256 SubjectPublicKeyInfo, up to the 0x04. */
static const uint8_t kP256SpkiPrefix[27] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
    0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04,
};

/* The raw 64-byte X||Y key that bootloader_support embeds from
 * CONFIG_SECURE_BOOT_VERIFICATION_KEY. */
extern const uint8_t s_verify_key[] asm("_binary_signature_verification_key_bin_start");

static bool s_trial;
static bool s_hold;

static bool upd_open(nvs_handle_t *h, nvs_open_mode_t mode)
{
    return nvs_open(GROK_UPD_NS, mode, h) == ESP_OK;
}

static uint8_t upd_get_u8(const char *key)
{
    nvs_handle_t h;
    uint8_t v = 0;

    if (upd_open(&h, NVS_READONLY))
    {
        if (nvs_get_u8(h, key, &v) != ESP_OK)
        {
            v = 0;
        }
        nvs_close(h);
    }
    return v;
}

static void upd_get_str(const char *key, char *out, size_t cap)
{
    nvs_handle_t h;
    size_t len = cap;

    out[0] = '\0';
    if (upd_open(&h, NVS_READONLY))
    {
        if (nvs_get_str(h, key, out, &len) != ESP_OK)
        {
            out[0] = '\0';
        }
        nvs_close(h);
    }
}

static void upd_set_last(const char *text)
{
    nvs_handle_t h;

    if (upd_open(&h, NVS_READWRITE))
    {
        nvs_set_str(h, "last", text);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void upd_set_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;

    if (upd_open(&h, NVS_READWRITE))
    {
        nvs_set_u8(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void upd_clear_pending(void)
{
    nvs_handle_t h;

    if (upd_open(&h, NVS_READWRITE))
    {
        nvs_erase_key(h, "pend");
        nvs_erase_key(h, "boots");
        nvs_erase_key(h, "hold");
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool upd_set_pending(const char *new_label, const char *prev_label)
{
    nvs_handle_t h;
    esp_err_t err;

    if (!upd_open(&h, NVS_READWRITE))
    {
        return false;
    }
    err = nvs_set_str(h, "new", new_label);
    if (err == ESP_OK)
    {
        err = nvs_set_str(h, "prev", prev_label);
    }
    if (err == ESP_OK)
    {
        err = nvs_set_u8(h, "boots", 0);
    }
    if (err == ESP_OK)
    {
        err = nvs_set_u8(h, "pend", 1);
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

/* Make the bootloader run `part` next. For ota_0 holding a USB-flashed,
 * unsigned image esp_ota_set_boot_partition() fails its signature check;
 * erasing otadata then makes the stock bootloader pick ota_0 ("No factory
 * image, trying OTA 0"), the same as flashing ota_data_initial.bin. */
static bool select_boot(const esp_partition_t *part)
{
    const esp_partition_t *otadata;
    esp_err_t err;

    if (part == NULL)
    {
        return false;
    }
    err = esp_ota_set_boot_partition(part);
    if (err == ESP_OK)
    {
        return true;
    }
    if (part->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0)
    {
        printf("update: cannot select %s (%s)\n", part->label, esp_err_to_name(err));
        return false;
    }
    otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata == NULL)
    {
        return false;
    }
    printf("update: %s is not signed (USB flash); resetting otadata so ota_0 boots\n", part->label);
    return esp_partition_erase_range(otadata, 0, otadata->size) == ESP_OK;
}

static void roll_back(const char *why)
{
    char prev[GROK_UPD_LABEL_MAX];
    char last[GROK_UPD_LAST_MAX];
    const esp_partition_t *part;

    upd_get_str("prev", prev, sizeof(prev));
    part = (prev[0] != '\0') ? esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, prev) : NULL;
    if (!select_boot(part))
    {
        /* Nothing to go back to: keep running this image, stop counting. */
        snprintf(last, sizeof(last), "rollback failed (%.40s); kept new image", why);
        printf("update: %s\n", last);
        upd_set_last(last);
        upd_clear_pending();
        s_trial = false;
        return;
    }
    snprintf(last, sizeof(last), "rolled back to %.15s: %.60s", prev, why);
    printf("update: %s. restarting\n", last);
    upd_set_last(last);
    upd_clear_pending();
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

void Grok_UpdateBootCheck(void)
{
    char new_label[GROK_UPD_LABEL_MAX];
    char last[GROK_UPD_LAST_MAX];
    const esp_partition_t *running = esp_ota_get_running_partition();
    unsigned boots;
    bool pending = upd_get_u8("pend") != 0;

    if (!pending || (running == NULL))
    {
        return;
    }
    upd_get_str("new", new_label, sizeof(new_label));
    boots = upd_get_u8("boots");
    switch (grok_ota_boot_decision(pending, strcmp(running->label, new_label) == 0, boots, GROK_UPD_MAX_BOOTS))
    {
    case GROK_OTA_BOOT_NOT_TAKEN:
        snprintf(last, sizeof(last), "%.15s did not boot; still on %.15s", new_label, running->label);
        printf("update: %s\n", last);
        upd_set_last(last);
        upd_clear_pending();
        break;
    case GROK_OTA_BOOT_ROLL_BACK:
        roll_back("new image never confirmed");
        break;
    case GROK_OTA_BOOT_TRIAL:
        upd_set_u8("boots", (uint8_t)(boots + 1));
        s_hold = upd_get_u8("hold") != 0;
        s_trial = true;
        printf("update: trial boot %u/%u of %s. confirms when Wi-Fi + console are up, else rolls back after %d s%s\n",
               boots + 1, (unsigned)GROK_UPD_MAX_BOOTS, esp_app_get_description()->version, GROK_UPD_HEALTH_S,
               s_hold ? " (test hold: will roll back)" : "");
        break;
    default:
        break;
    }
}

bool grok_update_trial(void)
{
    return s_trial;
}

void grok_update_tick(void)
{
    char last[GROK_UPD_LAST_MAX];

    if (!s_trial)
    {
        return;
    }
    if (!s_hold && grok_wifi_is_connected() && grok_console_running())
    {
        s_trial = false;
        snprintf(last, sizeof(last), "confirmed %.32s", esp_app_get_description()->version);
        upd_set_last(last);
        upd_clear_pending();
        printf("update: %s (Wi-Fi up, console up)\n", last);
        return;
    }
    if (esp_timer_get_time() > (int64_t)GROK_UPD_HEALTH_S * 1000000)
    {
        roll_back(s_hold ? "test hold" : "no Wi-Fi/console in time");
    }
}

static void key_id(uint8_t out[8])
{
    mbedtls_sha256_context ctx;
    uint8_t digest[32];

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, kP256SpkiPrefix, sizeof(kP256SpkiPrefix));
    mbedtls_sha256_update(&ctx, s_verify_key, 64);
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    memcpy(out, digest, 8);
}

static bool key_is_release(void)
{
    uint8_t id[8];

    key_id(id);
    return memcmp(id, kReleaseKeyId, sizeof(id)) == 0;
}

void grok_update_status(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t desc;
    char text[GROK_UPD_LAST_MAX];
    uint8_t id[8];

    printf("update: running %s @0x%06" PRIx32 " version %s\n", running ? running->label : "?",
           running ? running->address : 0, esp_app_get_description()->version);
    printf("update: next boot %s\n", boot ? boot->label : "?");
    if ((other != NULL) && (esp_ota_get_partition_description(other, &desc) == ESP_OK))
    {
        printf("update: other slot %s version %.32s\n", other->label, desc.version);
    }
    else
    {
        printf("update: other slot %s empty\n", other ? other->label : "?");
    }
    if (s_trial)
    {
        printf("update: state PENDING (trial boot %u/%u, rollback after %d s if Wi-Fi/console not up%s)\n",
               (unsigned)upd_get_u8("boots"), (unsigned)GROK_UPD_MAX_BOOTS, GROK_UPD_HEALTH_S,
               s_hold ? ", test hold" : "");
    }
    else
    {
        printf("update: state VALID\n");
    }
    upd_get_str("last", text, sizeof(text));
    printf("update: last %s\n", text[0] ? text : "-");
    key_id(id);
    printf("update: signing key %02x%02x%02x%02x%02x%02x%02x%02x %s\n", id[0], id[1], id[2], id[3], id[4], id[5],
           id[6], id[7], key_is_release() ? "(Joe release key)" : "(UNKNOWN KEY, not the release key)");
    if (!s_trial && upd_get_u8("hold"))
    {
        printf("update: test hold armed for the next update\n");
    }
}

void grok_update_arm_hold(void)
{
    upd_set_u8("hold", 1);
    printf("update: test hold armed. the next update will boot, not confirm, and roll back after %d s\n",
           GROK_UPD_HEALTH_S);
}

static esp_err_t open_follow(esp_http_client_handle_t client, int64_t *len, int *status)
{
    esp_err_t err = ESP_FAIL;

    for (int hop = 0; hop < 6; hop++)
    {
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK)
        {
            return err;
        }
        *len = esp_http_client_fetch_headers(client);
        *status = esp_http_client_get_status_code(client);
        if (!grok_ota_is_redirect(*status))
        {
            return ESP_OK;
        }
        esp_http_client_flush_response(client, NULL);
        err = esp_http_client_set_redirection(client);
        esp_http_client_close(client);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    return ESP_ERR_HTTP_MAX_REDIRECT;
}

/* Runs on the grok radio task (16 KB stack), BLE already released. */
bool grok_update_run(const char *url)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .buffer_size = 1536,
        .buffer_size_tx = 2048, /* GitHub's signed asset URLs are ~930 chars */
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
        .user_agent = "chromatic-grok-ota",
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = NULL;
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    bool ok = false;
    uint8_t *buf = NULL;
    char version[33];
    char last[GROK_UPD_LAST_MAX];
    int64_t len = 0;
    int status = 0;
    size_t total = 0;
    size_t next_note = 0;
    esp_err_t err;

    last[0] = '\0';
    version[0] = '\0';
    if ((running == NULL) || (target == NULL))
    {
        grok_publish("update: no OTA slot");
        return false;
    }
    if (s_trial)
    {
        grok_publish("update: current update not confirmed yet; wait for it");
        return false;
    }
    printf("update: %s -> %s from %s\n", running->label, target->label, url);
    if (!key_is_release())
    {
        printf("update: warning: this build does not trust the release key\n");
    }
    grok_mem_print("before update");

    buf = malloc(GROK_UPD_CHUNK);
    client = esp_http_client_init(&cfg);
    if ((buf == NULL) || (client == NULL))
    {
        snprintf(last, sizeof(last), "out of memory");
        goto done;
    }
    err = open_follow(client, &len, &status);
    if (err != ESP_OK)
    {
        snprintf(last, sizeof(last), "connect failed (%s)", esp_err_to_name(err));
        goto done;
    }
    if (status != 200)
    {
        snprintf(last, sizeof(last), "HTTP %d", status);
        goto done;
    }
    if ((len > 0) && ((uint64_t)len > target->size))
    {
        snprintf(last, sizeof(last), "image too big (%" PRId64 " B)", len);
        goto done;
    }
    grok_mem_print("after TLS");

    for (;;)
    {
        int n = esp_http_client_read(client, (char *)buf, GROK_UPD_CHUNK);

        if (n < 0)
        {
            snprintf(last, sizeof(last), "download error");
            goto done;
        }
        if (n == 0)
        {
            if (esp_http_client_is_complete_data_received(client))
            {
                break;
            }
            snprintf(last, sizeof(last), "download cut off at %u B", (unsigned)total);
            goto done;
        }
        if (!ota_open)
        {
            GrokOtaHead_t head = grok_ota_check_head(buf, (size_t)n, version, sizeof(version));

            if (head != GROK_OTA_HEAD_OK)
            {
                snprintf(last, sizeof(last), "rejected: %s", grok_ota_head_reason(head));
                goto done;
            }
            printf("update: image version %s, %" PRId64 " B\n", version, len);
            /* Sequential erase: one 4 KB sector at a time as data arrives,
             * not the whole 1.9 MB slot up front. */
            err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
            if (err != ESP_OK)
            {
                snprintf(last, sizeof(last), "ota begin failed (%s)", esp_err_to_name(err));
                goto done;
            }
            ota_open = true;
        }
        if (total + (size_t)n > target->size)
        {
            snprintf(last, sizeof(last), "image larger than slot");
            goto done;
        }
        err = esp_ota_write(ota, buf, (size_t)n);
        if (err != ESP_OK)
        {
            snprintf(last, sizeof(last), "flash write failed (%s)", esp_err_to_name(err));
            goto done;
        }
        total += (size_t)n;
        if (total >= next_note)
        {
            printf("update: %u KB\n", (unsigned)(total / 1024));
            next_note = total + 128 * 1024;
        }
    }
    if (!ota_open)
    {
        snprintf(last, sizeof(last), "empty download");
        goto done;
    }
    esp_http_client_close(client);
    printf("update: %u B received, verifying signature\n", (unsigned)total);
    /* esp_image_verify(): segment checksums, SHA-256, and (because of
     * CONFIG_SECURE_SIGNED_ON_UPDATE) the ECDSA signature block. */
    err = esp_ota_end(ota);
    ota_open = false;
    if (err != ESP_OK)
    {
        snprintf(last, sizeof(last), "rejected: %s",
                 (err == ESP_ERR_OTA_VALIDATE_FAILED) ? "bad or missing signature, or corrupt image" : esp_err_to_name(err));
        goto done;
    }
    if (!upd_set_pending(target->label, running->label))
    {
        snprintf(last, sizeof(last), "could not save rollback state");
        goto done;
    }
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK)
    {
        upd_clear_pending();
        snprintf(last, sizeof(last), "could not select %s (%s)", target->label, esp_err_to_name(err));
        goto done;
    }
    snprintf(last, sizeof(last), "installed %s in %s", version, target->label);
    ok = true;

done:
    if (ota_open)
    {
        esp_ota_abort(ota);
    }
    if (!ok && (total > 0))
    {
        /* Leave no half-written or rejected image that looks bootable:
         * "grok update status" then reports the slot as empty. */
        esp_partition_erase_range(target, 0, 4096);
    }
    if (client != NULL)
    {
        esp_http_client_cleanup(client);
    }
    free(buf);
    if (!ok)
    {
        char msg[GROK_UPD_LAST_MAX];

        snprintf(msg, sizeof(msg), "update failed: %.70s", last);
        upd_set_last(msg);
        grok_publish(msg);
        grok_mem_print("after update");
        return false;
    }
    upd_set_last(last);
    printf("update: %s. restarting\n", last);
    grok_publish("update: installed, restarting");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return true;
}

bool grok_update_rollback_now(void)
{
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t desc;
    char last[GROK_UPD_LAST_MAX];

    if ((other == NULL) || (esp_ota_get_partition_description(other, &desc) != ESP_OK))
    {
        grok_publish("update: other slot is empty");
        return false;
    }
    if (!select_boot(other))
    {
        grok_publish("update: other slot is not bootable");
        return false;
    }
    snprintf(last, sizeof(last), "manual switch to %.15s (%.32s)", other->label, desc.version);
    upd_set_last(last);
    upd_clear_pending();
    printf("update: %s. restarting\n", last);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return true;
}
