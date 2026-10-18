/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Feasibility spike: a direct TLS call from the ESP32 to the xAI Responses
 * API, with the same request shape as companion/proxy.py. Used only in
 * "grok mode direct", after BLE has been stopped to free heap.
 *
 * The API key is never printed or logged. Every buffer that held it is
 * zeroed before it is freed. esp_http_client keeps its own copy of the
 * Authorization header, which it frees (without zeroing) on cleanup.
 */
#include "grok_internal.h"

#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XAI_URL "https://api.x.ai/v1/responses"
#define XAI_MODEL "grok-4.7"
#define XAI_MAX_OUTPUT_TOKENS 180

/* Same system prompt as companion/proxy.py. */
static const char kSystem[] =
    "You are answering on a 160x144 handheld with a 5px font. "
    "Reply in short plain ASCII. No markdown, no lists, no emoji. "
    "A few sentences at most.";

enum {
    kBodyCap = 1600,  /* system + worst-case escaped 200-char prompt */
    kRawCap = 8192,   /* Responses API JSON; the text is usually near the start */
    kAuthCap = GROK_XAI_KEY_MAX + 8,
};

static void secure_free(char *buf, size_t cap)
{
    if (buf != NULL)
    {
        memset(buf, 0, cap);
        free(buf);
    }
}

void grok_mem_print(const char *label)
{
    const uint32_t icaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dcaps = MALLOC_CAP_DMA;

    printf("mem %s: int8 free=%u largest=%u min=%u | dma free=%u largest=%u min=%u\n",
           (label != NULL) ? label : "",
           (unsigned)heap_caps_get_free_size(icaps),
           (unsigned)heap_caps_get_largest_free_block(icaps),
           (unsigned)heap_caps_get_minimum_free_size(icaps),
           (unsigned)heap_caps_get_free_size(dcaps),
           (unsigned)heap_caps_get_largest_free_block(dcaps),
           (unsigned)heap_caps_get_minimum_free_size(dcaps));
    fflush(stdout);
}

static bool build_body(char *body, size_t cap, const char *prompt)
{
    size_t used;
    size_t escaped = 0;
    int n;

    n = snprintf(body, cap,
                 "{\"model\":\"%s\",\"input\":[{\"role\":\"system\",\"content\":\"%s\"},"
                 "{\"role\":\"user\",\"content\":\"",
                 XAI_MODEL, kSystem);
    if ((n <= 0) || ((size_t)n >= cap))
    {
        return false;
    }
    used = (size_t)n;
    if (!grok_text_json_escape(body + used, cap - used, prompt, &escaped))
    {
        return false;
    }
    used += escaped;
    n = snprintf(body + used, cap - used, "\"}],\"max_output_tokens\":%d}", XAI_MAX_OUTPUT_TOKENS);
    return (n > 0) && ((size_t)n < (cap - used));
}

bool grok_https_ask(const char *key, const char *prompt, char *reply, size_t reply_cap)
{
    char *body = NULL;
    char *raw = NULL;
    char *auth = NULL;
    char parsed[GROK_REPLY_MAX + 1];
    esp_http_client_handle_t client = NULL;
    esp_http_client_config_t cfg = {
        .url = XAI_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_err_t err;
    int body_len;
    int status = 0;
    int written = 0;
    size_t raw_len = 0;
    size_t dropped = 0;
    int64_t t0;
    bool ok = false;

    if ((reply == NULL) || (reply_cap == 0))
    {
        return false;
    }
    reply[0] = '\0';

#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS
    (void)key;
    (void)prompt;
    snprintf(reply, reply_cap, "https is not built in");
    return false;
#else
    if ((key == NULL) || (key[0] == '\0'))
    {
        snprintf(reply, reply_cap, "no xai key. grok key set <key>");
        return false;
    }

    body = malloc(kBodyCap);
    raw = malloc(kRawCap);
    auth = malloc(kAuthCap);
    if ((body == NULL) || (raw == NULL) || (auth == NULL))
    {
        snprintf(reply, reply_cap, "https: out of memory");
        goto done;
    }
    raw[0] = '\0';

    if (!build_body(body, kBodyCap, prompt))
    {
        snprintf(reply, reply_cap, "prompt too long");
        goto done;
    }
    body_len = (int)strlen(body);

    client = esp_http_client_init(&cfg);
    if (client == NULL)
    {
        snprintf(reply, reply_cap, "https init failed");
        goto done;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", "chromatic-grok");
    if (snprintf(auth, kAuthCap, "Bearer %s", key) >= kAuthCap)
    {
        snprintf(reply, reply_cap, "xai key is too long");
        goto done;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    memset(auth, 0, kAuthCap);

    grok_mem_print("before tls");
    t0 = esp_timer_get_time();
    err = esp_http_client_open(client, body_len);
    printf("tls: %s in %u ms\n", (err == ESP_OK) ? "connected" : "failed",
           (unsigned)((esp_timer_get_time() - t0) / 1000));
    grok_mem_print("after tls");
    if (err != ESP_OK)
    {
        snprintf(reply, reply_cap, "https connect failed (%s)", esp_err_to_name(err));
        goto done;
    }

    while (written < body_len)
    {
        int n = esp_http_client_write(client, body + written, body_len - written);
        if (n <= 0)
        {
            break;
        }
        written += n;
    }
    if (written != body_len)
    {
        snprintf(reply, reply_cap, "https write failed");
        goto done;
    }

    if (esp_http_client_fetch_headers(client) < 0)
    {
        snprintf(reply, reply_cap, "https no response headers");
        goto done;
    }
    status = esp_http_client_get_status_code(client);

    for (;;)
    {
        int n;

        if (raw_len < (kRawCap - 1))
        {
            n = esp_http_client_read(client, raw + raw_len, (int)(kRawCap - 1 - raw_len));
            if (n <= 0)
            {
                break;
            }
            raw_len += (size_t)n;
        }
        else
        {
            char sink[128];

            n = esp_http_client_read(client, sink, sizeof(sink));
            if (n <= 0)
            {
                break;
            }
            dropped += (size_t)n;
        }
    }
    raw[raw_len] = '\0';
    printf("https: status %d, %u bytes%s\n", status, (unsigned)(raw_len + dropped),
           (dropped > 0) ? " (truncated)" : "");
    grok_mem_print("after reply");

    if ((status >= 200) && (status < 300) && grok_text_responses_text(raw, parsed, sizeof(parsed)))
    {
        grok_text_fold_ascii(parsed);
        snprintf(reply, reply_cap, "%s", parsed);
        ok = true;
    }
    else if (status == 401)
    {
        snprintf(reply, reply_cap, "xai rejected the key (401). grok key set <key>");
    }
    else if (grok_text_responses_error(raw, parsed, sizeof(parsed)))
    {
        char clean[GROK_REPLY_MAX + 1];

        grok_text_redact(parsed, key, clean, sizeof(clean));
        snprintf(reply, reply_cap, "xai http %d: %s", status, clean);
        memset(clean, 0, sizeof(clean));
    }
    else
    {
        snprintf(reply, reply_cap, "xai http %d, no text", status);
    }
    memset(parsed, 0, sizeof(parsed));

done:
    if (client != NULL)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    secure_free(auth, kAuthCap);
    secure_free(body, kBodyCap);
    secure_free(raw, kRawCap);
    return ok;
#endif
}
