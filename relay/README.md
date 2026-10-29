# Chromatic Grok Bot reply relay

Tiny HTTP mailbox so a Grok Bot routine can drop a short reply for the Chromatic to poll.

Suitable to host on **grok.me** (Grok Build-style app): one process, file-backed JSON store, tokens from env only.

## Endpoints

| Method | Path | Auth | Body / query |
| --- | --- | --- | --- |
| `POST` | `/v1/reply` | `Authorization: Bearer $BOT_POST_TOKEN` | JSON `{device_id,msg_id,reply,suggestions[3]}` |
| `GET` | `/v1/reply` | `Authorization: Bearer $DEVICE_TOKEN` | `?device_id=&msg_id=` → `200` JSON, or `204` if not ready |
| `GET` | `/health` | none | `{ok:true}` |

Messages expire after 1 hour (`RELAY_TTL_SEC`). Reply ≤ 40 chars; each suggestion ≤ 17 chars. Rate limit ~60 req/min per role.

## Local run

```bash
export BOT_POST_TOKEN='long-random-bot-token'
export DEVICE_TOKEN='long-random-device-token'
cd relay
python3 app.py --host 127.0.0.1 --port 8787
```

## grok.me

Publish this folder as a Grok Build web app (Joe's go). Set `BOT_POST_TOKEN` and `DEVICE_TOKEN` in the app env. Prefer HTTPS URL like `https://<slug>.grok.me` for the device `grok bot relay` command.

Do not commit tokens. Rotate if leaked.
