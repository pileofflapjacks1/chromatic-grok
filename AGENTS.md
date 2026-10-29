# chromatic-grok

Modified ModRetro Chromatic MCU firmware. Upstream is `upstream` (https://github.com/ModRetro/oss-chromatic-console-mcu), commit `63ed3b62b16768ad2415c9e3b865b438ca40fcc6`. Do not add an `origin` remote unless Joe asks. Do not commit or push unless Joe asks.

- ESP-IDF v5.3, target ESP32, 4 MB flash, DIO. Do not enable QIO. GPIO9/GPIO10 are the FPGA UART.
- Do not change FPGA protocol, `button.c`, `fpga_rx.c`, `gfx.c`, or the settings namespace.
- Do not `idf.py erase-flash`. NVS must stay at `0x9000`.
- Grok code is `components/grok/`. Proxy mode: the device speaks `http://` to a LAN proxy and the xAI key stays in `XAI_API_KEY` on the computer. Direct mode (`grok mode direct`): HTTPS to api.x.ai with BLE stopped; the key is in NVS `grokcfg/xaikey`. Never print, log or echo the key.
- RAM is tight (direct-mode low point ~10 KB on hardware). Do not add static DRAM or long-lived heap without measuring.
- Wireless updates (`grok update`, `components/grok/grok_update.c`) accept only images signed for the key in `CONFIG_SECURE_BOOT_VERIFICATION_KEY`. Never put a private signing key on the build box or in git; only public keys go in `keys/`. Never enable secure boot, flash encryption, or anything that burns eFuses, and never reflash the bootloader at `0x1000`. A USB app reflash writes `0x10000` and `ota_data_initial.bin` at `0x3F0000` together.
- Phone typing (`grok web`, `components/grok/grok_web.c`): plain HTTP on port 80, off by default, 6-digit PIN with lockout. Never serve or log the key or token. In direct mode the server must stay stopped during TLS (`grok_web_pause/resume`). Portable helpers are in `grok_web_util.c` and host-tested.
- Light sleep is skipped while `Grok_WantsRadioAwake()` is true. Default is awake.
- Host checks: `sh tests/run_host.sh`. That is not an IDF build and not a hardware test.
- Grok Bot mode (`docs/grok-bot-mode.md`, `relay/`): webhook + grok.me relay. Tokens/keys in NVS/env only; never print full secrets. Do not publish the relay or create bot routines unless Joe asks.
- `web/` is the browser companion (TanStack Start). It is not part of the ESP-IDF build. `cd web && npm test && npm run build`.
