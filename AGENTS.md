# chromatic-grok

Modified ModRetro Chromatic MCU firmware. Upstream is `upstream` (https://github.com/ModRetro/oss-chromatic-console-mcu), commit `63ed3b62b16768ad2415c9e3b865b438ca40fcc6`. Do not add an `origin` remote unless Joe asks. Do not commit or push unless Joe asks.

- ESP-IDF v5.3, target ESP32, 4 MB flash, DIO. Do not enable QIO. GPIO9/GPIO10 are the FPGA UART.
- Do not change FPGA protocol, `button.c`, `fpga_rx.c`, `gfx.c`, or the settings namespace.
- Do not `idf.py erase-flash`. NVS must stay at `0x9000`.
- Grok code is `components/grok/`. Proxy mode: the device speaks `http://` to a LAN proxy and the xAI key stays in `XAI_API_KEY` on the computer. Direct mode (`grok mode direct`): HTTPS to api.x.ai with BLE stopped; the key is in NVS `grokcfg/xaikey`. Never print, log or echo the key.
- RAM is tight (direct-mode low point ~10 KB on hardware). Do not add static DRAM or long-lived heap without measuring.
- Light sleep is skipped while `Grok_WantsRadioAwake()` is true. Default is awake.
- Host checks: `sh tests/run_host.sh`. That is not an IDF build and not a hardware test.
- `web/` is the browser companion (TanStack Start). It is not part of the ESP-IDF build. `cd web && npm test && npm run build`.
