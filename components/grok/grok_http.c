#include "grok_internal.h"

#include "esp_http_client.h"

#include <stdio.h>
#include <string.h>

typedef struct HttpResp {
    char *buf;
    size_t cap;
    size_t len;
} HttpResp_t;

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    HttpResp_t *resp;
    size_t room;
    size_t n;

    if ((evt == NULL) || (evt->event_id != HTTP_EVENT_ON_DATA) || (evt->data_len <= 0))
    {
        return ESP_OK;
    }

    resp = (HttpResp_t *)evt->user_data;
    if ((resp == NULL) || (resp->buf == NULL) || (resp->cap < 2))
    {
        return ESP_OK;
    }

    room = resp->cap - 1 - resp->len;
    n = (size_t)evt->data_len;
    if (n > room)
    {
        n = room;
    }
    if (n > 0)
    {
        memcpy(resp->buf + resp->len, evt->data, n);
        resp->len += n;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}

bool grok_http_ask(const char *url, const char *prompt, char *reply, size_t reply_cap)
{
    static char body[768];
    static char raw[1536];
    size_t used = 0;
    size_t escaped = 0;
    int tail;
    HttpResp_t resp;
    esp_http_client_config_t cfg = {
        .timeout_ms = 20000,
        .event_handler = on_http,
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .disable_auto_redirect = true,
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int status;
    char parsed[GROK_REPLY_MAX + 1];

    if ((reply == NULL) || (reply_cap == 0))
    {
        return false;
    }
    reply[0] = '\0';

    if ((url == NULL) || (strncmp(url, "http://", 7) != 0))
    {
        snprintf(reply, reply_cap, "proxy url must start with http://");
        return false;
    }

    used = (size_t)snprintf(body, sizeof(body), "{\"prompt\":\"");
    if ((used == 0) || (used >= sizeof(body)))
    {
        snprintf(reply, reply_cap, "prompt too long");
        return false;
    }
    if (!grok_text_json_escape(body + used, sizeof(body) - used, prompt, &escaped))
    {
        snprintf(reply, reply_cap, "prompt too long");
        return false;
    }
    used += escaped;
    tail = snprintf(body + used, sizeof(body) - used, "\",\"max_chars\":%d}", GROK_REPLY_MAX);
    if ((tail < 0) || ((size_t)tail >= (sizeof(body) - used)))
    {
        snprintf(reply, reply_cap, "prompt too long");
        return false;
    }

    raw[0] = '\0';
    resp.buf = raw;
    resp.cap = sizeof(raw);
    resp.len = 0;

    cfg.url = url;
    cfg.user_data = &resp;
    client = esp_http_client_init(&cfg);
    if (client == NULL)
    {
        snprintf(reply, reply_cap, "http init failed");
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", "chromatic-grok");
    esp_http_client_set_post_field(client, body, (int)strlen(body));
    err = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        snprintf(reply, reply_cap, "http %s", esp_err_to_name(err));
        return false;
    }

    if (grok_text_json_get_string(raw, "reply", parsed, sizeof(parsed)))
    {
        snprintf(reply, reply_cap, "%s", parsed);
        return (status >= 200) && (status < 300);
    }
    if (grok_text_json_get_string(raw, "error", parsed, sizeof(parsed)))
    {
        snprintf(reply, reply_cap, "%s", parsed);
        return false;
    }
    if ((raw[0] != '\0') && (raw[0] != '{') && (raw[0] != '['))
    {
        snprintf(reply, reply_cap, "%s", raw);
        return (status >= 200) && (status < 300);
    }

    snprintf(reply, reply_cap, "http %d", status);
    return false;
}
