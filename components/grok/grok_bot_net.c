/* Device-side Grok Bot webhook POST + relay poll.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bot_net.h"

#include "grok_internal.h"

#include "sdkconfig.h"

#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kPostBodyCap = 512,
    kRawCap = 1024,
    kAuthCap = 208,
    kUrlCap = 256,
};

static void secure_free(char *buf, size_t cap)
{
    if (buf != NULL)
    {
        memset(buf, 0, cap);
        free(buf);
    }
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool https_exchange(const char *url, bool method_get,
                           const char *auth_bearer, const char *body,
                           int body_len, char *raw, size_t raw_cap, int *status_out)
{
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS
    (void)url;
    (void)method_get;
    (void)auth_bearer;
    (void)body;
    (void)body_len;
    (void)raw;
    (void)raw_cap;
    (void)status_out;
    if (status_out)
    {
        *status_out = 0;
    }
    return false;
#else
    esp_http_client_config_t cfg = {
        .url = url,
        .method = method_get ? HTTP_METHOD_GET : HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client;
    char *auth = NULL;
    esp_err_t err;
    int status = 0;
    size_t raw_len = 0;
    bool ok = false;

    if ((raw == NULL) || (raw_cap < 2) || (url == NULL))
    {
        return false;
    }
    raw[0] = '\0';
    if (status_out)
    {
        *status_out = 0;
    }

    client = esp_http_client_init(&cfg);
    if (client == NULL)
    {
        return false;
    }
    esp_http_client_set_header(client, "User-Agent", "chromatic-grok");
    if (!method_get)
    {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if ((auth_bearer != NULL) && (auth_bearer[0] != '\0'))
    {
        auth = malloc(kAuthCap);
        if ((auth == NULL) || (snprintf(auth, kAuthCap, "Bearer %s", auth_bearer) >= kAuthCap))
        {
            secure_free(auth, kAuthCap);
            esp_http_client_cleanup(client);
            return false;
        }
        esp_http_client_set_header(client, "Authorization", auth);
        memset(auth, 0, kAuthCap);
    }

    err = esp_http_client_open(client, method_get ? 0 : body_len);
    if (err != ESP_OK)
    {
        goto done;
    }
    if (!method_get && (body != NULL) && (body_len > 0))
    {
        int written = 0;
        while (written < body_len)
        {
            int n = esp_http_client_write(client, body + written, body_len - written);
            if (n <= 0)
            {
                goto done;
            }
            written += n;
        }
    }
    if (esp_http_client_fetch_headers(client) < 0)
    {
        goto done;
    }
    status = esp_http_client_get_status_code(client);
    if (status_out)
    {
        *status_out = status;
    }
    for (;;)
    {
        int n;
        if (raw_len < (raw_cap - 1))
        {
            n = esp_http_client_read(client, raw + raw_len, (int)(raw_cap - 1 - raw_len));
            if (n <= 0)
            {
                break;
            }
            raw_len += (size_t)n;
        }
        else
        {
            char sink[64];
            n = esp_http_client_read(client, sink, sizeof(sink));
            if (n <= 0)
            {
                break;
            }
        }
    }
    raw[raw_len] = '\0';
    ok = true;

done:
    if (client != NULL)
    {
        char *hdr = NULL;
        if ((esp_http_client_get_header(client, "Authorization", &hdr) == ESP_OK) &&
            (hdr != NULL))
        {
            memset(hdr, 0, strlen(hdr));
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    secure_free(auth, kAuthCap);
    return ok;
#endif
}

bool grok_bot_ask(const char *webhook_url, const char *webhook_key,
                  const char *relay_url, const char *relay_token,
                  const char *text, const char *device_id, const char *msg_id,
                  GrokBotReply_t *out, char *err, size_t err_cap)
{
    char *body = NULL;
    char *raw = NULL;
    char *full_url = NULL;
    char path[128];
    int status = 0;
    GrokBotPoll_t poll;
    bool ok = false;

    if ((err != NULL) && (err_cap > 0))
    {
        err[0] = '\0';
    }
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out));
    }
    if ((webhook_url == NULL) || (strncmp(webhook_url, "https://", 8) != 0))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot webhook must be https://");
        }
        return false;
    }
    if ((relay_url == NULL) || (strncmp(relay_url, "https://", 8) != 0))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot relay must be https://");
        }
        return false;
    }
    if ((text == NULL) || (device_id == NULL) || (msg_id == NULL) || (out == NULL))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot ask bad args");
        }
        return false;
    }

    body = malloc(kPostBodyCap);
    raw = malloc(kRawCap);
    full_url = malloc(kUrlCap);
    if ((body == NULL) || (raw == NULL) || (full_url == NULL))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot: out of memory");
        }
        goto done;
    }

    if (!grok_bot_build_post_json(body, kPostBodyCap, text, device_id, msg_id))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot: prompt too long");
        }
        goto done;
    }

    grok_ui_set_body(GROK_BOT_WAIT_LABEL);
    if (!https_exchange(webhook_url, false, webhook_key, body, (int)strlen(body),
                        raw, kRawCap, &status))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot webhook connect failed");
        }
        goto done;
    }
    if ((status < 200) || (status >= 300))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot webhook http %d", status);
        }
        goto done;
    }

    if (!grok_bot_build_poll_path(path, sizeof(path), device_id, msg_id))
    {
        if (err && err_cap)
        {
            snprintf(err, err_cap, "bot: bad poll path");
        }
        goto done;
    }
    {
        size_t base = strlen(relay_url);
        while ((base > 0) && (relay_url[base - 1] == '/'))
        {
            base--;
        }
        if (base + strlen(path) + 1 > kUrlCap)
        {
            if (err && err_cap)
            {
                snprintf(err, err_cap, "bot: relay url too long");
            }
            goto done;
        }
        memcpy(full_url, relay_url, base);
        memcpy(full_url + base, path, strlen(path) + 1);
    }

    grok_bot_poll_begin(&poll, now_ms(), GROK_BOT_POLL_INTERVAL_MS, GROK_BOT_POLL_TIMEOUT_MS);
    for (;;)
    {
        GrokBotPollAction_t act = grok_bot_poll_tick(&poll, now_ms());

        if (act == GROK_BOT_POLL_TIMEOUT)
        {
            if (err && err_cap)
            {
                snprintf(err, err_cap, "bot timed out");
            }
            goto done;
        }
        if (act == GROK_BOT_POLL_IDLE)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        /* DO_GET */
        grok_ui_set_body(GROK_BOT_WAIT_LABEL);
        status = 0;
        raw[0] = '\0';
        if (!https_exchange(full_url, true, relay_token, NULL, 0, raw, kRawCap, &status))
        {
            if (err && err_cap)
            {
                snprintf(err, err_cap, "bot relay connect failed");
            }
            goto done;
        }
        if (status == 204)
        {
            continue;
        }
        if (status == 404)
        {
            continue;
        }
        if ((status >= 200) && (status < 300) && grok_bot_parse_reply_json(raw, out))
        {
            ok = true;
            goto done;
        }
        if ((status == 401) || (status == 403))
        {
            if (err && err_cap)
            {
                snprintf(err, err_cap, "bot relay auth failed");
            }
            goto done;
        }
    }

done:
    secure_free(body, kPostBodyCap);
    secure_free(raw, kRawCap);
    secure_free(full_url, kUrlCap);
    return ok;
}
