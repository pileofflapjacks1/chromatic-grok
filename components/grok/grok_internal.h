/* Shared calls inside the grok component. Not part of the stock OSD API.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "grok_pick.h"
#include "grok_bots.h"
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
/* Queue a phone-page question on the grok ask path. False when the queue is
 * full; publishes nothing itself. */
bool grok_submit_web(const char *prompt);
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
/* Phone page on/off, NVS grokcfg/web. Default off. */
bool grok_store_set_web(bool on);
bool grok_store_get_web(void);
/* Custom preset questions (0-based slot), NVS "q1".."q8". Serial `grok q` only. */
bool grok_store_get_question(int idx, char *out, size_t out_cap);
bool grok_store_set_question(int idx, const char *text);
void grok_store_clear_question(int idx);
/* Effective question: the custom one if set, else the built-in. */
bool grok_question_get(int idx, char *out, size_t out_cap, bool *custom);
/* Per-bot last prompt + reply snippet, NVS "b0".."b3" (one packed string each). */
bool grok_store_set_bot_last(int idx, const char *prompt, const char *reply);
bool grok_store_get_bot_last(int idx, char *prompt, size_t prompt_cap,
                             char *reply, size_t reply_cap);

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
/* Show the static bot/team menu. */
void grok_ui_open_bots(void);
/* Show the connecting loader (Carty G frames); always at least one cycle. */
void grok_ui_open_loader(void);

/* Phone typing page (grok_web.c), port 80, off by default. kick starts the
 * server when enabled, Wi-Fi has an IP and (direct mode) BLE is released;
 * down stops it and forgets the PIN; pause/resume stop it around TLS.
 * kick does not steal the Grok overlay; show_pin does (explicit entry). */
void grok_web_init(void);
void grok_web_kick(void);
void grok_web_show_pin(void);
void grok_web_down(void);
void grok_web_pause(void);
void grok_web_resume(void);
/* Hands a published reply to the page if it asked the running job. */
void grok_web_answer(const char *text);
void grok_web_status(void);
void grok_web_mem_print(void);

/* True once the masked-echo console task is reading input. */
bool grok_console_running(void);

/* Wireless update (grok_update.c). run/rollback_now execute on the radio
 * task; they restart the chip on success. */
#define GROK_UPDATE_DEFAULT_URL \
    "https://github.com/pileofflapjacks1/chromatic-grok/releases/latest/download/chromatic_grok.signed.bin"
bool grok_update_run(const char *url);
bool grok_update_rollback_now(void);
void grok_update_status(void);
void grok_update_arm_hold(void);
/* True while a freshly installed image has not been confirmed yet. */
bool grok_update_trial(void);
/* Called about once a second by the radio task while a trial runs. */
void grok_update_tick(void);
