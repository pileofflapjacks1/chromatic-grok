/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Phone typing: a tiny HTTP server on port 80 of the Chromatic's Wi-Fi IP.
 *   GET  /        one inline page (PIN, question, Send)
 *   POST /ask     pin, q   -> queued on the same job queue as `grok ask`
 *   GET  /answer  ?pin=    -> "idle" | "wait" | "done" line, then the answer
 * Off by default (`grok web on`, NVS grokcfg/web). A random 6-digit PIN is
 * made at every start and shown on the Grok screen. 5 wrong PINs lock the
 * page for 60 s. Plain HTTP: the PIN and question cross the LAN unencrypted.
 * The xAI key and proxy token are never sent to the phone.
 *
 * RAM: the server (task stack, sockets, scratch) and the request/answer
 * buffers are allocated only while the server runs. In direct mode it starts
 * only after BLE has released its RAM, and the radio task pauses it (stops
 * the server, keeps the PIN and answer) during the TLS call and updates.
 */
#include "grok_internal.h"
#include "grok_web_util.h"

#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kStack = 4096,
    kBodyCap = 768,
};

typedef enum {
    WEB_IDLE = 0,
    WEB_WAIT,
    WEB_DONE,
} WebState_t;

/* Heap, only while the page is enabled and the network is up. */
typedef struct {
    char body[kBodyCap + 1];
    char q[GROK_PROMPT_MAX + 1];
    char answer[GROK_REPLY_MAX + 1];
} WebBuf_t;

static SemaphoreHandle_t s_mu;
static httpd_handle_t s_server;
static WebBuf_t *s_buf;
static GrokWebGuard_t s_guard;
static WebState_t s_state;
static bool s_paused;
static char s_pin[GROK_WEB_PIN_LEN + 1];
static uint32_t s_stack_min;

static const char kPage[] =
    "<!DOCTYPE html><html><head><meta name=viewport content=\"width=device-width\">"
    "<title>Chromatic Grok</title><style>body{font:16px sans-serif;margin:12px}"
    "input,textarea,button{font:inherit;width:100%;box-sizing:border-box;margin:4px 0}"
    "pre{white-space:pre-wrap}</style></head><body><h3>Ask Grok on the Chromatic</h3>"
    "<input id=p inputmode=numeric maxlength=6 placeholder=PIN>"
    "<textarea id=q rows=4 maxlength=200 placeholder=Question></textarea>"
    "<button id=b>Send</button><pre id=o></pre><script>"
    "var o=document.getElementById('o'),t;"
    "function v(i){return document.getElementById(i).value}"
    "function poll(){fetch('/answer?pin='+encodeURIComponent(v('p'))).then(function(r){return r.text()})"
    ".then(function(s){var n=s.indexOf('\\n'),k=s.slice(0,n);o.textContent=s.slice(n+1);"
    "if(k=='wait'){o.textContent='Thinking...';t=setTimeout(poll,2000)}})"
    ".catch(function(){t=setTimeout(poll,3000)})}"
    "document.getElementById('b').onclick=function(){clearTimeout(t);o.textContent='Sending...';"
    "fetch('/ask',{method:'POST',body:new URLSearchParams({pin:v('p'),q:v('q')})})"
    ".then(function(r){return r.text().then(function(s){o.textContent=s;if(r.ok)poll()})})"
    ".catch(function(){o.textContent='No answer from the Chromatic.'})}"
    "</script></body></html>";

static void lock(void)
{
    if (s_mu != NULL)
    {
        xSemaphoreTake(s_mu, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_mu != NULL)
    {
        xSemaphoreGive(s_mu);
    }
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void note_stack(void)
{
    uint32_t free_b = (uint32_t)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));

    if ((s_stack_min == 0) || (free_b < s_stack_min))
    {
        s_stack_min = free_b;
    }
}

static esp_err_t send_text(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, text);
}

/* Caller holds the lock. Returns NULL when the PIN is accepted. */
static const char *pin_refusal(const char *pin, char *msg, size_t msg_cap)
{
    GrokWebPin_t r = grok_web_guard_check(&s_guard, now_ms(), s_pin, pin);

    if (r == GROK_WEB_PIN_OK)
    {
        return NULL;
    }
    if (r == GROK_WEB_PIN_LOCKED)
    {
        snprintf(msg, msg_cap, "Too many wrong PINs. Wait %u s.", (unsigned)grok_web_guard_wait_s(&s_guard, now_ms()));
        return "429 Too Many Requests";
    }
    snprintf(msg, msg_cap, "Wrong PIN.");
    return "403 Forbidden";
}

static esp_err_t on_page(httpd_req_t *req)
{
    note_stack();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPage, sizeof(kPage) - 1);
}

static esp_err_t on_ask(httpd_req_t *req)
{
    char pin[GROK_WEB_PIN_LEN + 2];
    char msg[48];
    const char *status;
    size_t got = 0;

    note_stack();
    if ((req->content_len == 0) || (req->content_len > kBodyCap))
    {
        return send_text(req, "413 Payload Too Large", "Question is empty or too long.");
    }
    lock();
    if (s_buf == NULL)
    {
        unlock();
        return send_text(req, "503 Service Unavailable", "Not ready.");
    }
    while (got < req->content_len)
    {
        int n = httpd_req_recv(req, s_buf->body + got, req->content_len - got);

        if (n <= 0)
        {
            unlock();
            return send_text(req, "400 Bad Request", "Request was cut off.");
        }
        got += (size_t)n;
    }
    s_buf->body[got] = '\0';
    pin[0] = '\0';
    grok_web_form_get(s_buf->body, got, "pin", pin, sizeof(pin));
    status = pin_refusal(pin, msg, sizeof(msg));
    if (status != NULL)
    {
        unlock();
        return send_text(req, status, msg);
    }
    if (!grok_web_form_get(s_buf->body, got, "q", s_buf->q, sizeof(s_buf->q)) ||
        (grok_web_sanitize(s_buf->q) == 0))
    {
        unlock();
        return send_text(req, "400 Bad Request", "Question must be 1 to 200 characters.");
    }
    if (s_state == WEB_WAIT)
    {
        unlock();
        return send_text(req, "409 Conflict", "Busy with the last question.");
    }
    s_state = WEB_WAIT;
    s_buf->answer[0] = '\0';
    if (!grok_submit_web(s_buf->q))
    {
        s_state = WEB_IDLE;
        unlock();
        return send_text(req, "409 Conflict", "Busy. Try again soon.");
    }
    unlock();
    printf("web: question queued\n");
    return send_text(req, "200 OK", "Asked. Waiting for Grok...");
}

static esp_err_t on_answer(httpd_req_t *req)
{
    char query[40];
    char pin[GROK_WEB_PIN_LEN + 2];
    char msg[48];
    const char *status;
    esp_err_t err;

    note_stack();
    pin[0] = '\0';
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        grok_web_form_get(query, strlen(query), "pin", pin, sizeof(pin));
    }
    lock();
    status = pin_refusal(pin, msg, sizeof(msg));
    if (status != NULL)
    {
        unlock();
        return send_text(req, status, msg);
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if ((s_state != WEB_DONE) || (s_buf == NULL))
    {
        err = httpd_resp_sendstr(req, (s_state == WEB_WAIT) ? "wait\n" : "idle\n");
        unlock();
        return err;
    }
    /* Two chunks; no copy of the answer on this stack. */
    err = httpd_resp_send_chunk(req, "done\n", 5);
    if (err == ESP_OK)
    {
        err = httpd_resp_send_chunk(req, s_buf->answer, HTTPD_RESP_USE_STRLEN);
    }
    if (err == ESP_OK)
    {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    unlock();
    return err;
}

/* Caller holds the lock. */
static bool server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = on_page },
        { .uri = "/ask", .method = HTTP_POST, .handler = on_ask },
        { .uri = "/answer", .method = HTTP_GET, .handler = on_answer },
    };

    cfg.stack_size = kStack;
    cfg.max_open_sockets = 2;
    cfg.backlog_conn = 2;
    cfg.max_uri_handlers = 3;
    cfg.max_resp_headers = 4;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    if (httpd_start(&s_server, &cfg) != ESP_OK)
    {
        s_server = NULL;
        return false;
    }
    for (size_t i = 0; i < (sizeof(uris) / sizeof(uris[0])); i++)
    {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
    return true;
}

/* Caller must NOT hold the lock: httpd_stop() waits for the server task,
 * which may be waiting for the lock inside a handler. */
static bool server_stop(bool pause)
{
    httpd_handle_t h;

    lock();
    h = s_server;
    s_server = NULL;
    if ((h != NULL) && pause)
    {
        s_paused = true;
    }
    unlock();
    if (h != NULL)
    {
        httpd_stop(h);
    }
    return h != NULL;
}

static void show_pin(void)
{
    char ip[32];
    char text[96];

    grok_wifi_format(ip, sizeof(ip));
    snprintf(text, sizeof(text), "PHONE TYPING\n\nOpen on your phone:\nhttp://%s\n\nPIN %s", ip, s_pin);
    grok_ui_set_body(text);
    printf("web: on http://%s pin %s\n", ip, s_pin);
}

void grok_web_init(void)
{
    if (s_mu == NULL)
    {
        s_mu = xSemaphoreCreateMutex();
    }
}

static bool can_run(void)
{
    if (!grok_store_get_web() || !grok_wifi_is_connected())
    {
        return false;
    }
    /* TLS in direct mode needs BLE's RAM first; the page waits for it. */
    return (grok_store_get_mode() == GROK_MODE_PROXY) || grok_ble_released();
}

void grok_web_kick(void)
{
    bool shown = false;

    if (s_mu == NULL)
    {
        return;
    }
    lock();
    if ((s_server == NULL) && !s_paused && can_run())
    {
        bool fresh = (s_buf == NULL);

        if (fresh)
        {
            s_buf = calloc(1, sizeof(*s_buf));
        }
        if (s_buf == NULL)
        {
            printf("web: out of memory\n");
        }
        else if (!server_start())
        {
            printf("web: server did not start\n");
            free(s_buf);
            s_buf = NULL;
        }
        else if (fresh)
        {
            grok_web_pin_format(esp_random(), s_pin);
            memset(&s_guard, 0, sizeof(s_guard));
            s_state = WEB_IDLE;
            s_stack_min = 0;
            shown = true;
        }
    }
    unlock();
    if (shown)
    {
        show_pin();
    }
}

void grok_web_down(void)
{
    if (s_mu == NULL)
    {
        return;
    }
    server_stop(false);
    lock();
    free(s_buf);
    s_buf = NULL;
    s_paused = false;
    s_state = WEB_IDLE;
    memset(s_pin, 0, sizeof(s_pin));
    unlock();
}

void grok_web_pause(void)
{
    if (s_mu == NULL)
    {
        return;
    }
    server_stop(true);
}

void grok_web_resume(void)
{
    bool was;

    if (s_mu == NULL)
    {
        return;
    }
    lock();
    was = s_paused;
    s_paused = false;
    unlock();
    if (was)
    {
        grok_web_kick();
    }
}

void grok_web_answer(const char *text)
{
    lock();
    if ((s_buf != NULL) && (s_state == WEB_WAIT))
    {
        snprintf(s_buf->answer, sizeof(s_buf->answer), "%s", (text != NULL) ? text : "");
        s_state = WEB_DONE;
    }
    unlock();
}

void grok_web_status(void)
{
    char ip[32];
    char line[160];

    grok_wifi_format(ip, sizeof(ip));
    lock();
    if (!grok_store_get_web())
    {
        snprintf(line, sizeof(line), "web: off\n");
    }
    else if (s_buf == NULL)
    {
        snprintf(line, sizeof(line), "web: on, waiting for %s\n",
                 !grok_wifi_is_connected() ? "wifi" : "ble to stop (direct mode; runs after the first ask)");
    }
    else
    {
        snprintf(line, sizeof(line), "web: on http://%s pin %s%s%s\n", ip, s_pin,
                 s_paused ? " (paused for tls)" : "",
                 (grok_web_guard_wait_s(&s_guard, now_ms()) > 0) ? " (locked out)" : "");
    }
    unlock();
    grok_say(line);
}

void grok_web_mem_print(void)
{
    lock();
    printf("mem web: %s buf=%u stack=%u stack_min_free=%u\n",
           (s_server != NULL) ? "running" : (s_paused ? "paused" : "stopped"),
           (unsigned)((s_buf != NULL) ? sizeof(*s_buf) : 0), (unsigned)kStack, (unsigned)s_stack_min);
    unlock();
    fflush(stdout);
}
