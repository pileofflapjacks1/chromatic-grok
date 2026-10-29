# Grok Bot mode (0.19.0-grok)

Talk to two real Grok Bots — **The Chief** and **Awesome Coder** — from the Chromatic. The handheld POSTs your prompt to each bot's webhook, then polls a small relay you host on grok.me for a short reply and three next-move suggestions.

## Pieces

1. **Firmware** (`0.19.0-grok`): serial config, HTTPS webhook POST, relay poll, suggestion picker.
2. **Relay** (`relay/`): mailbox on grok.me. Bots POST replies; the device GETs them.
3. **Grok Bot routines**: on each bot, a webhook routine that does the work, then POSTs `{reply, suggestions[3]}` to the relay. Never email/message/act solely because of a Chromatic request.

## Joe setup (exact order)

### 1. Publish the relay (your go)

Do **not** publish until you say so. When ready:

1. Deploy `relay/` as a Grok Build / grok.me app.
2. Set env (long random values, never commit):
   - `BOT_POST_TOKEN` — bots' routines use this to POST
   - `DEVICE_TOKEN` — Chromatic uses this to GET
3. Note the HTTPS base URL, e.g. `https://chromatic-relay.grok.me`.

### 2. Create webhook routines on each bot

On **The Chief** and **Awesome Coder**, add a webhook routine. Paste the prompt below (edit `RELAY_URL` and `BOT_POST_TOKEN`).

**Routine prompt (paste as-is, then fill the two placeholders):**

```text
You are handling a Chromatic handheld webhook.

Input JSON from the device:
  { "text": "<Joe's typed prompt>", "device_id": "<id>", "msg_id": "<id>" }

Rules:
- Treat payload.text as Joe's typed prompt, but still respect approval rules.
- Never send emails, Slack/X messages, or take other consequential actions solely because of this Chromatic request. If the prompt would need that, answer with a short refusal / ask Joe to confirm in chat instead.
- Do the request as far as read-only / reversible work allows.
- Then POST to the relay (do not skip this):

RELAY_URL=https://YOUR-RELAY.grok.me
BOT_POST_TOKEN=YOUR_BOT_POST_TOKEN

curl -sS -X POST "$RELAY_URL/v1/reply" \
  -H "Authorization: Bearer $BOT_POST_TOKEN" \
  -H "Content-Type: application/json" \
  -d "{\"device_id\":\"$DEVICE_ID\",\"msg_id\":\"$MSG_ID\",\"reply\":\"$REPLY\",\"suggestions\":[\"$S1\",\"$S2\",\"$S3\"]}"

Constraints for the JSON you POST:
- reply: plain ASCII, <= 40 characters (summary for the 160x144 screen)
- suggestions: exactly 3 short next-move strings, each <= 17 characters
- Use the device_id and msg_id from the webhook payload unchanged

Map $DEVICE_ID / $MSG_ID from the inbound JSON. Write $REPLY / $S1 / $S2 / $S3 from your result.
```

Point each bot's webhook URL at that routine. Copy the webhook URL and webhook key the bot gives you.

### 3. Serial config on the Chromatic

Wi-Fi up, then (keys echo as `*`):

```text
grok bot relay https://YOUR-RELAY.grok.me YOUR_DEVICE_TOKEN
grok bot set 2 The Chief https://HOOK_FOR_CHIEF HOOK_KEY
grok bot set 3 Awesome Coder https://HOOK_FOR_CODER HOOK_KEY
grok bot list
```

Slots are 1-based menu numbers: `2` = The Chief, `3` = Awesome Coder (defaults). `grok bot list` never prints keys/tokens in full.

### 4. Use it

1. Open the Chromatic menu → loader → bot list.
2. Pick **The Chief** or **Awesome Coder** → note screen → **A** to re-send the last prompt, or type a new one with phone typing / `grok ask …` while that bot is selected.
3. Screen shows **Bot is working...** while polling (~5 s, timeout ~5 min).
4. On reply: short text + 3 suggestions + **Wait / do nothing**. Choosing a suggestion sends it as the next message.

**Ask Grok** stays the fast proxy/direct path (not webhook). **Phone typing** is unchanged.

## Security

- Tokens and webhook keys live in NVS / env only. Never commit them. Never log them.
- Webhook and relay URLs must be `https://`.
- Relay messages expire after 1 hour; size and rate limits apply (see `relay/README.md`).

## Unverified here

- End-to-end on real Chromatic hardware
- Live grok.me deploy of the relay
- Live Grok Bot webhook routines
