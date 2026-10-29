/* Grok Bot webhook + relay helpers (host-tested).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GROK_BOT_SUGGEST_COUNT 3
#define GROK_BOT_SUGGEST_MAX 17
#define GROK_BOT_MSG_ID_MAX 32
#define GROK_BOT_DEVICE_ID_MAX 24
#define GROK_BOT_WAIT_LABEL "Bot is working..."
#define GROK_BOT_WAIT_OPTION "Wait / do nothing"

/* Default poll / timeout (ms). Override in tests. */
#define GROK_BOT_POLL_INTERVAL_MS 5000u
#define GROK_BOT_POLL_TIMEOUT_MS 300000u

typedef enum {
    GROK_BOT_POLL_IDLE = 0,
    GROK_BOT_POLL_DO_GET,   /* caller should GET the relay */
    GROK_BOT_POLL_TIMEOUT,
} GrokBotPollAction_t;

typedef struct GrokBotPoll {
    uint32_t start_ms;
    uint32_t last_poll_ms;
    uint32_t interval_ms;
    uint32_t timeout_ms;
    bool started;
} GrokBotPoll_t;

typedef struct GrokBotReply {
    char reply[41]; /* GROK_BOTS_LAST_REPLY_MAX + 1; keep in sync */
    char suggest[GROK_BOT_SUGGEST_COUNT][GROK_BOT_SUGGEST_MAX + 1];
    int suggest_n;
} GrokBotReply_t;

/* Build POST JSON {"text","device_id","msg_id"}. False if it does not fit. */
bool grok_bot_build_post_json(char *out, size_t cap, const char *text,
                              const char *device_id, const char *msg_id);

/* Parse relay GET/POST body. Missing suggestions become "". */
bool grok_bot_parse_reply_json(const char *json, GrokBotReply_t *out);

/* Start a poll window at now_ms. */
void grok_bot_poll_begin(GrokBotPoll_t *p, uint32_t now_ms, uint32_t interval_ms,
                         uint32_t timeout_ms);

/* Advance the poll clock. First call and each interval -> DO_GET. */
GrokBotPollAction_t grok_bot_poll_tick(GrokBotPoll_t *p, uint32_t now_ms);

/* Build GET path /v1/reply?device_id=&msg_id= (no host). */
bool grok_bot_build_poll_path(char *out, size_t cap, const char *device_id,
                              const char *msg_id);

/* Mask a secret for console list: "abcd…wxyz" or "(unset)". */
void grok_bot_mask_secret(char *out, size_t cap, const char *secret);

/* Option row for the post-reply picker: idx 0..2 = suggestions, 3 = wait. */
size_t grok_bot_format_option(char *out, size_t cap, int idx, bool selected,
                              const GrokBotReply_t *reply, size_t cols);
