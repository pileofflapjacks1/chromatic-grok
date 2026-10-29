/* Device-side Grok Bot webhook POST + relay poll.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "grok_bot_proto.h"

#include <stdbool.h>
#include <stddef.h>

/* POST {text,device_id,msg_id} to webhook_url with Bearer key over HTTPS.
 * On success, polls relay_url/v1/reply until reply, timeout, or error.
 * Fills reply_out and suggestions. Shows progress via grok_ui_set_body. */
bool grok_bot_ask(const char *webhook_url, const char *webhook_key,
                  const char *relay_url, const char *relay_token,
                  const char *text, const char *device_id, const char *msg_id,
                  GrokBotReply_t *out, char *err, size_t err_cap);
