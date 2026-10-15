# Chromatic Grok protocol

The serial console and the BLE RX characteristic use the same ASCII lines. A line ends with `\n`. `\r` is ignored. The console prompt is `mcu> ` at 115200 8N1.

Quote a value that contains spaces. The console keeps at most 32 arguments, so quote a long prompt as one argument. A BLE line is about 240 bytes; the last word keeps the rest of that line. Passwords are not printed back.

## Wi-Fi

```
wifi status
wifi connect
wifi disconnect
wifi clear
wifi set <ssid> <password>
```

Leave the password off for an open network. Example:

```
wifi set "Cafe WiFi" "correct horse"
```

`wifi status` prints `wifi: ssid=... state=up|down|off ip=...`.

Credentials live in the NVS namespace `grokcfg` (`ssid`, `pass`, `url`, `token`). They are not written into the IDF Wi-Fi NVS copy.

## Grok link

```
grok status
grok url
grok url set http://192.168.1.20:8787/v1/ask
grok token set <token>
grok token clear
grok ble unpair
grok ask <prompt>
grok show <text>
grok retry
grok open
grok close
grok page next
grok page prev
grok page 2
grok sleep off
grok sleep on
```

`grok page` numbers are 1-based. `grok sleep off` keeps the radios awake. `grok sleep on` allows the stock light sleep, which drops Wi-Fi and BLE.

`grok token set` saves the proxy token; it is sent as `Authorization: Bearer <token>` and never printed. A `401` from the proxy shows "proxy rejected the token".

`grok ask` posts JSON to the saved URL:

```json
{"prompt":"What is a Game Boy?","max_chars":640}
```

The body must be `http://` to a proxy you control. `https://` is rejected. The firmware stores no API key.

A successful proxy responds with `{"reply":"..."}`. `{"error":"..."}` is shown as text. If the body is not JSON, plain text is shown.

Final replies, including errors, are printed and notified as:

```
GROK_BEGIN
text
GROK_END
```

The on-screen placeholder `...` is not framed. One request runs at a time. A second ask returns `busy`.

## Buttons

Open the stock menu first. Buttons are delivered to the MCU while that menu is up.

- Start opens or closes the reply overlay.
- System tab, GROK row, A opens the overlay.
- While the overlay is open: Up/Down change page, A sends the last prompt again, B or Start closes it.

## Screen

Replies are folded to printable ASCII. The overlay shows 32 columns and 12 lines per page, up to 40 wrapped lines and 640 characters. The header looks like `GROK 2/3 192.168.1.20`.

## BLE

Peripheral name: `ChromaticGrok`.

| Role | UUID |
| --- | --- |
| Service | `8f3c0001-6b2a-4d5e-9c11-0a1b2c3d4e5f` |
| RX write | `8f3c0002-6b2a-4d5e-9c11-0a1b2c3d4e5f` |
| TX notify and read | `8f3c0003-6b2a-4d5e-9c11-0a1b2c3d4e5f` |

Write a command to RX, including the newline. Subscribe to TX for `GROK_BEGIN` / `GROK_END`. Notifications are chunked to the ATT payload, at most 180 bytes. Security: LE Secure Connections, bonding, MITM via passkey entry (the device is DisplayOnly and shows a random 6-digit passkey on screen). RX is write-with-response only and requires an encrypted + authenticated link (`WRITE_ENC | WRITE_AUTHEN`); TX read requires `READ_ENC | READ_AUTHEN`; notifications are suppressed until the link is authenticated. The firmware also re-checks the link state on every access. Up to 3 bonds are kept in NVS; `grok ble unpair` clears them. Write-without-response is no longer accepted.

## Proxy

`companion/proxy.py` listens on `127.0.0.1:8787` by default. `GROK_PROXY_ALLOW_LAN=1` allows `0.0.0.0` or another `--host`.

- `GROK_PROXY_TOKEN` (≥16 chars) is required to start. `POST /v1/ask` without `Authorization: Bearer <token>` returns `401 {"error":"unauthorized"}`. The compare is constant-time (`hmac.compare_digest`).
- `GET /health` returns `{"ok": true}` without auth and reveals nothing else.
- `POST /v1/ask` with the device JSON calls `POST https://api.x.ai/v1/responses`.
- Model defaults to `grok-4.7`. Override with `XAI_MODEL`.
- The key is `XAI_API_KEY` in the proxy process only. It is never returned or logged; upstream error text is redacted.

The proxy asks for a short plain-ASCII reply and caps it for the screen.
