#include "grok_internal.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *NVS_NS = "grokcfg";

static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_started;
static bool s_connected;
static bool s_hold;
static int s_retries;
static char s_ssid[GROK_SSID_MAX + 1];
static char s_pass[GROK_PASS_MAX + 1];
static char s_ip[16];
static char s_url[GROK_URL_MAX + 1];
static char s_token[GROK_TOKEN_MAX + 1];

static void lock(void)
{
    if (s_lock != NULL)
    {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock != NULL)
    {
        xSemaphoreGive(s_lock);
    }
}

static bool store_open(void)
{
    esp_err_t err;

    if (s_nvs != 0)
    {
        return true;
    }

    err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK)
    {
        printf("grok: nvs open failed (%s)\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void load_str(const char *key, char *dst, size_t cap)
{
    size_t len = cap;
    esp_err_t err = nvs_get_str(s_nvs, key, dst, &len);
    if (err != ESP_OK)
    {
        dst[0] = '\0';
    }
}

void grok_store_init(void)
{
    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateMutex();
    }
    if (!store_open())
    {
        return;
    }
    load_str("ssid", s_ssid, sizeof(s_ssid));
    load_str("pass", s_pass, sizeof(s_pass));
    load_str("url", s_url, sizeof(s_url));
    load_str("token", s_token, sizeof(s_token));
    if (s_ssid[0] != '\0')
    {
        printf("wifi: stored ssid=%s\n", s_ssid);
    }
}

bool grok_store_set_wifi(const char *ssid, const char *pass)
{
    esp_err_t a;
    esp_err_t b;
    esp_err_t c;

    if (!store_open())
    {
        return false;
    }
    a = nvs_set_str(s_nvs, "ssid", ssid);
    b = nvs_set_str(s_nvs, "pass", (pass != NULL) ? pass : "");
    c = nvs_commit(s_nvs);
    return (a == ESP_OK) && (b == ESP_OK) && (c == ESP_OK);
}

void grok_store_clear_wifi(void)
{
    if (!store_open())
    {
        return;
    }
    nvs_erase_key(s_nvs, "ssid");
    nvs_erase_key(s_nvs, "pass");
    nvs_commit(s_nvs);
}

bool grok_store_set_url(const char *url)
{
    esp_err_t a;
    esp_err_t b;

    if (!store_open())
    {
        return false;
    }
    a = nvs_set_str(s_nvs, "url", (url != NULL) ? url : "");
    b = nvs_commit(s_nvs);
    if ((a != ESP_OK) || (b != ESP_OK))
    {
        return false;
    }
    lock();
    snprintf(s_url, sizeof(s_url), "%s", (url != NULL) ? url : "");
    unlock();
    return true;
}

void grok_store_get_url(char *out, size_t out_cap)
{
    if ((out == NULL) || (out_cap == 0))
    {
        return;
    }
    lock();
    snprintf(out, out_cap, "%s", s_url);
    unlock();
}

/* The proxy token is a shared secret. It is stored in NVS and sent as a
 * Bearer header, and is never printed. */
bool grok_store_set_token(const char *token)
{
    esp_err_t a;
    esp_err_t b;

    if ((token == NULL) || (token[0] == '\0') || (strlen(token) > GROK_TOKEN_MAX))
    {
        return false;
    }
    if (!store_open())
    {
        return false;
    }
    a = nvs_set_str(s_nvs, "token", token);
    b = nvs_commit(s_nvs);
    if ((a != ESP_OK) || (b != ESP_OK))
    {
        return false;
    }
    lock();
    snprintf(s_token, sizeof(s_token), "%s", token);
    unlock();
    return true;
}

void grok_store_clear_token(void)
{
    if (store_open())
    {
        nvs_erase_key(s_nvs, "token");
        nvs_commit(s_nvs);
    }
    lock();
    memset(s_token, 0, sizeof(s_token));
    unlock();
}

void grok_store_get_token(char *out, size_t out_cap)
{
    if ((out == NULL) || (out_cap == 0))
    {
        return;
    }
    lock();
    snprintf(out, out_cap, "%s", s_token);
    unlock();
}

bool grok_store_has_token(void)
{
    bool has;

    lock();
    has = (s_token[0] != '\0');
    unlock();
    return has;
}

static bool xai_key_ok(const char *key)
{
    size_t len;

    if (key == NULL)
    {
        return false;
    }
    len = strlen(key);
    if ((len < GROK_XAI_KEY_MIN) || (len > GROK_XAI_KEY_MAX))
    {
        return false;
    }
    for (size_t i = 0; i < len; i++)
    {
        if ((key[i] <= ' ') || (key[i] > '~'))
        {
            return false;
        }
    }
    return true;
}

/* The xAI key lives only in NVS ("xaikey"). It is not cached in RAM, never
 * printed, and callers zero their copy after use. */
bool grok_store_set_xai_key(const char *key)
{
    esp_err_t a;
    esp_err_t b;

    if (!xai_key_ok(key) || !store_open())
    {
        return false;
    }
    lock();
    a = nvs_set_str(s_nvs, "xaikey", key);
    b = nvs_commit(s_nvs);
    unlock();
    return (a == ESP_OK) && (b == ESP_OK);
}

void grok_store_clear_xai_key(void)
{
    if (!store_open())
    {
        return;
    }
    lock();
    nvs_erase_key(s_nvs, "xaikey");
    nvs_commit(s_nvs);
    unlock();
}

bool grok_store_get_xai_key(char *out, size_t out_cap)
{
    size_t len = out_cap;
    esp_err_t err;

    if ((out == NULL) || (out_cap == 0))
    {
        return false;
    }
    out[0] = '\0';
    if (!store_open())
    {
        return false;
    }
    lock();
    err = nvs_get_str(s_nvs, "xaikey", out, &len);
    unlock();
    if (err != ESP_OK)
    {
        memset(out, 0, out_cap);
        return false;
    }
    return out[0] != '\0';
}

bool grok_store_has_xai_key(void)
{
    size_t len = 0;
    esp_err_t err;

    if (!store_open())
    {
        return false;
    }
    lock();
    err = nvs_get_str(s_nvs, "xaikey", NULL, &len);
    unlock();
    return (err == ESP_OK) && (len > 1);
}

bool grok_store_set_mode(GrokMode_t mode)
{
    esp_err_t a;
    esp_err_t b;

    if (!store_open())
    {
        return false;
    }
    lock();
    a = nvs_set_u8(s_nvs, "mode", (uint8_t)mode);
    b = nvs_commit(s_nvs);
    unlock();
    return (a == ESP_OK) && (b == ESP_OK);
}

GrokMode_t grok_store_get_mode(void)
{
    uint8_t v = GROK_MODE_PROXY;

    if (!store_open())
    {
        return GROK_MODE_PROXY;
    }
    lock();
    if (nvs_get_u8(s_nvs, "mode", &v) != ESP_OK)
    {
        v = GROK_MODE_PROXY;
    }
    unlock();
    return (v == GROK_MODE_DIRECT) ? GROK_MODE_DIRECT : GROK_MODE_PROXY;
}

bool grok_store_set_web(bool on)
{
    esp_err_t a;
    esp_err_t b;

    if (!store_open())
    {
        return false;
    }
    lock();
    a = nvs_set_u8(s_nvs, "web", on ? 1 : 0);
    b = nvs_commit(s_nvs);
    unlock();
    return (a == ESP_OK) && (b == ESP_OK);
}

bool grok_store_get_web(void)
{
    uint8_t v = 0;

    if (!store_open())
    {
        return false;
    }
    lock();
    if (nvs_get_u8(s_nvs, "web", &v) != ESP_OK)
    {
        v = 0;
    }
    unlock();
    return v == 1;
}

/* Custom preset questions: NVS keys "q1".."q8". Read on demand, not cached. */
static void question_key(int idx, char key[4])
{
    key[0] = 'q';
    key[1] = (char)('1' + idx);
    key[2] = '\0';
}

bool grok_store_get_question(int idx, char *out, size_t out_cap)
{
    char key[4];
    size_t len = out_cap;
    esp_err_t err;

    if ((out == NULL) || (out_cap == 0))
    {
        return false;
    }
    out[0] = '\0';
    if ((idx < 0) || (idx >= GROK_PICK_COUNT) || !store_open())
    {
        return false;
    }
    question_key(idx, key);
    lock();
    err = nvs_get_str(s_nvs, key, out, &len);
    unlock();
    if (err != ESP_OK)
    {
        out[0] = '\0';
        return false;
    }
    return out[0] != '\0';
}

bool grok_store_set_question(int idx, const char *text)
{
    char key[4];
    esp_err_t a;
    esp_err_t b;

    if ((idx < 0) || (idx >= GROK_PICK_COUNT) || (text == NULL) || (text[0] == '\0') ||
        (strlen(text) > GROK_PROMPT_MAX) || !store_open())
    {
        return false;
    }
    question_key(idx, key);
    lock();
    a = nvs_set_str(s_nvs, key, text);
    b = nvs_commit(s_nvs);
    unlock();
    return (a == ESP_OK) && (b == ESP_OK);
}

void grok_store_clear_question(int idx)
{
    char key[4];

    if ((idx < 0) || (idx >= GROK_PICK_COUNT) || !store_open())
    {
        return;
    }
    question_key(idx, key);
    lock();
    nvs_erase_key(s_nvs, key);
    nvs_commit(s_nvs);
    unlock();
}

static void fill_config(wifi_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    lock();
    memcpy(cfg->sta.ssid, s_ssid, strnlen(s_ssid, GROK_SSID_MAX));
    memcpy(cfg->sta.password, s_pass, strnlen(s_pass, GROK_PASS_MAX));
    unlock();
    cfg->sta.threshold.authmode = (cfg->sta.password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    cfg->sta.pmf_cfg.capable = true;
    cfg->sta.pmf_cfg.required = false;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_START))
    {
        bool connect = false;
        lock();
        connect = (!s_hold && (s_ssid[0] != '\0'));
        unlock();
        if (connect)
        {
            esp_wifi_connect();
        }
        return;
    }

    if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_DISCONNECTED))
    {
        bool retry = false;
        int attempt = 0;
        lock();
        s_connected = false;
        s_ip[0] = '\0';
        if (!s_hold)
        {
            s_retries++;
            attempt = s_retries;
            retry = (s_retries <= 10);
        }
        unlock();
        grok_web_down();
        if (s_hold)
        {
            printf("wifi: disconnected\n");
            return;
        }
        printf("wifi: disconnected, retry %d\n", attempt);
        if (retry)
        {
            esp_wifi_connect();
        }
        else
        {
            printf("wifi: stopped retrying. Run `wifi connect` to try again.\n");
        }
        return;
    }

    if ((base == IP_EVENT) && (id == IP_EVENT_STA_GOT_IP))
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        lock();
        s_connected = true;
        s_retries = 0;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip, sizeof(s_ip));
        printf("wifi: connected ip=%s\n", s_ip);
        unlock();
        grok_web_kick();
    }
}

bool grok_wifi_start(void)
{
    esp_err_t err;
    bool have;
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    err = esp_netif_init();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE))
    {
        printf("wifi: netif init failed (%s)\n", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE))
    {
        printf("wifi: event loop failed (%s)\n", esp_err_to_name(err));
        return false;
    }

    esp_netif_create_default_wifi_sta();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK)
    {
        printf("wifi: init failed (%s)\n", esp_err_to_name(err));
        return false;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    lock();
    have = (s_ssid[0] != '\0');
    unlock();
    if (have)
    {
        wifi_config_t cfg;
        fill_config(&cfg);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        printf("wifi: start failed (%s)\n", esp_err_to_name(err));
        return false;
    }

    s_started = true;
    if (!have)
    {
        printf("wifi: no credentials. Use `wifi set <ssid> <password>`\n");
    }
    else
    {
        printf("wifi: station starting ssid=%s\n", s_ssid);
    }
    return true;
}

bool grok_wifi_set(const char *ssid, const char *pass)
{
    wifi_config_t cfg;
    bool started;

    if ((ssid == NULL) || (ssid[0] == '\0') || (strlen(ssid) > GROK_SSID_MAX))
    {
        grok_say("wifi: ssid must be 1 to 32 characters\n");
        return false;
    }
    if ((pass != NULL) && (strlen(pass) > GROK_PASS_MAX))
    {
        grok_say("wifi: password must be 0 to 64 characters\n");
        return false;
    }
    if (!grok_store_set_wifi(ssid, pass))
    {
        grok_say("wifi: could not save credentials\n");
        return false;
    }

    lock();
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    snprintf(s_pass, sizeof(s_pass), "%s", (pass != NULL) ? pass : "");
    s_hold = false;
    s_retries = 0;
    started = s_started;
    unlock();

    printf("wifi: saved ssid=%s\n", ssid);
    if (!started)
    {
        return true;
    }

    fill_config(&cfg);
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK)
    {
        grok_say("wifi: set config failed\n");
        return false;
    }
    if (esp_wifi_connect() != ESP_OK)
    {
        grok_say("wifi: connect failed\n");
        return false;
    }
    grok_say("wifi: connecting\n");
    return true;
}

void grok_wifi_clear(void)
{
    grok_store_clear_wifi();
    lock();
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_hold = true;
    s_connected = false;
    s_ip[0] = '\0';
    unlock();
    if (s_started)
    {
        esp_wifi_disconnect();
    }
    grok_say("wifi: credentials cleared\n");
}

void grok_wifi_connect(void)
{
    wifi_config_t cfg;
    bool have;
    bool started;

    lock();
    s_hold = false;
    s_retries = 0;
    have = (s_ssid[0] != '\0');
    started = s_started;
    unlock();

    if (!have)
    {
        grok_say("wifi: no credentials\n");
        return;
    }
    if (!started)
    {
        grok_say("wifi: radio is not up yet\n");
        return;
    }

    fill_config(&cfg);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (esp_wifi_connect() != ESP_OK)
    {
        grok_say("wifi: connect failed\n");
        return;
    }
    grok_say("wifi: connecting\n");
}

void grok_wifi_disconnect(void)
{
    lock();
    s_hold = true;
    unlock();
    if (s_started)
    {
        esp_wifi_disconnect();
    }
    grok_say("wifi: disconnecting\n");
}

void grok_wifi_print_status(void)
{
    char line[160];

    lock();
    snprintf(line, sizeof(line),
             "wifi: ssid=%s state=%s ip=%s\n",
             (s_ssid[0] != '\0') ? s_ssid : "(none)",
             s_connected ? "up" : (s_started ? "down" : "off"),
             (s_ip[0] != '\0') ? s_ip : "-");
    unlock();
    grok_say(line);
}

bool grok_wifi_is_connected(void)
{
    bool up;
    lock();
    up = s_connected;
    unlock();
    return up;
}

bool grok_wifi_has_ssid(void)
{
    bool have;
    lock();
    have = (s_ssid[0] != '\0');
    unlock();
    return have;
}

void grok_wifi_format(char *out, size_t out_cap)
{
    if ((out == NULL) || (out_cap == 0))
    {
        return;
    }
    lock();
    if (s_connected && (s_ip[0] != '\0'))
    {
        snprintf(out, out_cap, "%s", s_ip);
    }
    else if ((s_ssid[0] != '\0') && !s_hold)
    {
        snprintf(out, out_cap, "joining");
    }
    else if (s_ssid[0] != '\0')
    {
        snprintf(out, out_cap, "idle");
    }
    else
    {
        snprintf(out, out_cap, "no wifi");
    }
    unlock();
}
