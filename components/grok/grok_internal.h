/* Shared calls inside the grok component. Not part of the stock OSD API.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "grok_text.h"

#include <stddef.h>
#include <stdbool.h>

#define GROK_URL_MAX 160
#define GROK_SSID_MAX 32
#define GROK_PASS_MAX 64
#define GROK_TOKEN_MAX 64
/* xAI API keys are ~84+ characters. Separate from the proxy token. */
#define GROK_XAI_KEY_MIN 16
#define GROK_XAI_KEY_MAX 192

typedef enum {
    GROK_MODE_PROXY = 0,
    GROK_MODE_DIRECT = 1,
} GrokMode_t;

struct _lv_obj_t;

void grok_say(const char *text);
void grok_publish(const char *text);
void grok_exec(int argc, char **argv);
void grok_exec_line(char *line);
bool grok_submit_prompt(const char *prompt);
void grok_submit_show(const char *text);
bool grok_retry(void);

void grok_store_init(void);
bool grok_store_set_wifi(const char *ssid, const char *pass);
void grok_store_clear_wifi(void);
bool grok_store_set_url(const char *url);
void grok_store_get_url(char *out, size_t out_cap);
bool grok_store_set_token(const char *token);
void grok_store_clear_token(void);
void grok_store_get_token(char *out, size_t out_cap);
bool grok_store_has_token(void);
/* The xAI key is read from NVS on demand and never cached or printed. */
bool grok_store_set_xai_key(const char *key);
void grok_store_clear_xai_key(void);
bool grok_store_get_xai_key(char *out, size_t out_cap);
bool grok_store_has_xai_key(void);
bool grok_store_set_mode(GrokMode_t mode);
GrokMode_t grok_store_get_mode(void);

bool grok_wifi_start(void);
bool grok_wifi_set(const char *ssid, const char *pass);
void grok_wifi_clear(void);
void grok_wifi_connect(void);
void grok_wifi_disconnect(void);
void grok_wifi_print_status(void);
bool grok_wifi_is_connected(void);
bool grok_wifi_has_ssid(void);
void grok_wifi_format(char *out, size_t out_cap);

bool grok_ble_start(void);
void grok_ble_queue_text(const char *text);
void grok_ble_format(char *out, size_t out_cap);
void grok_ble_unpair(void);
/* Stop and deinit NimBLE and the controller, then hand the controller RAM to
 * the heap. Must not run on the NimBLE host task. BLE stays off until reboot. */
bool grok_ble_stop(void);
bool grok_ble_released(void);

bool grok_http_ask(const char *url, const char *token, const char *prompt, char *reply, size_t reply_cap);
/* Direct TLS call to api.x.ai. Prints heap numbers around the handshake. */
bool grok_https_ask(const char *key, const char *prompt, char *reply, size_t reply_cap);
void grok_mem_print(const char *label);

void grok_ui_init(struct _lv_obj_t *screen);
void grok_ui_open(void);
void grok_ui_close(void);
bool grok_ui_is_open(void);
void grok_ui_set_body(const char *text);
void grok_ui_page(int delta);
void grok_ui_set_page(int page);
