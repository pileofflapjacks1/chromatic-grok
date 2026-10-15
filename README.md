# chromatic-grok

Custom MCU firmware for the ModRetro Chromatic. The stock menu and game path stay in place. This tree adds a Wi-Fi station, a BLE peripheral, and a short text link to Grok.

This is a modified version of the ModRetro Chromatic MCU firmware, distributed under GPL-3.0-only (upstream is GPL-3.0; new files are GPL-3.0-or-later):

- Upstream: https://github.com/ModRetro/oss-chromatic-console-mcu
- Base commit: `63ed3b62b16768ad2415c9e3b865b438ca40fcc6` ("Publish v4.6")
- License: [LICENSE](LICENSE). See [NOTICE](NOTICE).

The Chromatic MCU is an ESP32-U4WDH with 4 MB of flash and no PSRAM. The screen is 160×144. The ESP32 is the composite USB serial port used for flashing and the `mcu>` console. GPIO9 and GPIO10 are the FPGA UART, so the flash stays in DIO mode.

App version: `0.14.0-grok`. The version string shown in the system menu is `4.6g`. The FPGA does not check that string.

Shiny Relay features (link cable over Wi-Fi, cart flashing, and the rest) are not in this firmware.

## Disclaimer

- **Unofficial.** Not affiliated with, endorsed by, or sponsored by ModRetro or Nintendo. ModRetro and Chromatic are trademarks of ModRetro; Game Boy is a trademark of Nintendo.
- **Flash at your own risk.** Custom firmware can leave the handheld unbootable. Back up the full 4 MB flash first (section 1) and keep that file. There is no OTA and no rollback slot; recovery is over USB.
- **Wi-Fi and BLE are for personal experimentation only.** Turning on the ESP32 radios may fall outside the device's certification (FCC/IC/CE or others). Do not ship or sell devices running this firmware.
- **Battery life is shorter** while the Grok link keeps the radios awake (the default). `grok sleep on` restores stock light sleep.
- No warranty. See sections 15 and 16 of the GPL in LICENSE.

## What you get

- Stock OSD, with a GROK row on the System tab and a Start shortcut while the menu is open.
- Wi-Fi station mode. SSID and password are stored in NVS (`grokcfg`) and can be set from the serial console.
- BLE peripheral named `ChromaticGrok`. It speaks the same line commands as the console.
- `grok ask` posts the prompt to an `http://` proxy you run. The proxy holds the xAI key.
- `grok show` displays text the computer already has, so a reply can reach the screen with no device Wi-Fi.
- Replies use the stock `fingfai` font, 32 columns, 12 lines per page.

## 1. Back up the stock flash

Do this before the first custom flash. The backup is the exact image you can write back later.

Connect the Chromatic over USB and find the port (`/dev/cu.usbmodem…` on macOS, `/dev/ttyACM0` on Linux). From an ESP-IDF v5.3 shell:

```bash
. $HOME/esp-idf/export.sh
esptool.py --chip esp32 -p "$PORT" -b 460800 read_flash 0x0 0x400000 chromatic-stock-4mb.bin
```

Keep that 4 MB file. It is the full flash, not the smaller merged image that upstream sometimes attaches to a release. Do not write `merged-flash_*.bin` at a guessed offset.

`idf.py erase-flash` wipes NVS, including menu settings and any Wi-Fi credentials. Do not use it unless you mean to.

## 2. Install ESP-IDF v5.3

The project needs ESP-IDF v5.3 and the ESP32 toolchain. https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32/get-started/

Linux (Ubuntu) summary, same as upstream:

```bash
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp-idf"
cd "$HOME/esp-idf"
./install.sh esp32
```

On Windows, use the Espressif installer and select v5.3 and the ESP32 target.

Every new shell needs the export before `idf.py` or `esptool.py`:

```bash
. $HOME/esp-idf/export.sh
```

Upstream suggests an alias rather than sourcing this from every terminal:

```bash
alias get_idf='. $HOME/esp-idf/export.sh'
```

## 3. Build and flash

```bash
. $HOME/esp-idf/export.sh
cd ~/Projects/chromatic-grok
idf.py set-target esp32
idf.py build
idf.py -p "$PORT" flash monitor
```

Leave the monitor with `Ctrl-]`.

`idf.py set-target esp32` once is enough. The project name is `chromatic_grok`, so the app binary is not named `mcu_fw`.

The partition table keeps NVS at `0x9000` / 24 KB, the same slot as the stock single-app table. The factory app slot grows to `0x1F0000` (about 2 MB) so Wi-Fi and BLE fit. Flashing the new partition table is expected the first time. That flash does not move NVS. Menu settings can survive an app reflash. A full chip erase will not.

If `idf.py` rejects a name in `sdkconfig.defaults`, fix that symbol from the v5.3 Kconfig instead of guessing. Wi-Fi and BLE together are tight on this chip: buffers are reduced, AMPDU is off, and HTTPS is off.

## 4. Restore stock firmware

Write the backup back:

```bash
. $HOME/esp-idf/export.sh
esptool.py --chip esp32 -p "$PORT" -b 460800 write_flash 0x0 chromatic-stock-4mb.bin
```

The official MR Updater can also return a Chromatic to current stock firmware. Downloads: https://modretro.com/pages/downloads. That updater installs ModRetro's latest stock image. It does not restore your backup file.

## 5. Configure Wi-Fi

Open a serial terminal at 115200, or use the companion below. The prompt is `mcu>`.

```text
wifi set MySSID MyPassword
wifi status
```

Quote an SSID or password that contains spaces:

```text
wifi set "Cafe WiFi" "correct horse"
```

An empty password means an open network: `wifi set MySSID`. The password is saved and is not printed. `wifi status` reports `up`, `down`, or `off` and the IP address. The device retries a few times, then stops until `wifi connect`.

`wifi clear` forgets the credentials. `wifi disconnect` stays disconnected until `wifi connect`.

Light sleep is off by default so the association can stay up. Battery life is shorter while that is true. `grok sleep on` allows the stock light sleep again. Wi-Fi and BLE drop when the MCU sleeps. `grok sleep off` stays awake.

## 6. Talk to Grok

The API key stays on a computer you control. The handheld only sends plain HTTP to a URL you set.

In one terminal, from this repo:

```bash
cd ~/Projects/chromatic-grok
python3 -m pip install -r companion/requirements.txt
export XAI_API_KEY="your key"
python3 companion/proxy.py
```

The proxy listens on `0.0.0.0:8787` with no authentication. Use it on a LAN you trust. `GET /health` returns `{"ok": true}`. Copy `companion/.env.example` if you want the variable names in one place. Do not commit a filled-in `.env`.

Find this computer's LAN address, then on the Chromatic:

```text
grok url set http://192.168.1.20:8787/v1/ask
grok ask What is a Game Boy?
```

The screen shows `...`, then the reply. Open the Chromatic menu and press Start, or open System → GROK and press A. Up and Down change page. A sends the last prompt again. B or Start closes the overlay.

The proxy calls `POST https://api.x.ai/v1/responses` with model `grok-4.7` (`XAI_MODEL` overrides it) and asks for a short plain-ASCII answer. Override the model only if you know that name is valid for your key.

### Companion

```bash
python3 companion/chromatic_grok.py --port "$PORT" wifi-set "MySSID" "MyPassword"
python3 companion/chromatic_grok.py --port "$PORT" url-set "http://192.168.1.20:8787/v1/ask"
python3 companion/chromatic_grok.py --port "$PORT" ask "What is a Game Boy?"
```

`--port` defaults to `CHROMATIC_PORT`, then the first `/dev/cu.usbmodem*`, then `/dev/ttyACM0`. Opening the port can reboot the MCU. The script waits for `mcu>`.

If the handheld is not on Wi-Fi, the computer can call xAI and push the text:

```bash
export XAI_API_KEY="your key"
python3 companion/chromatic_grok.py --port "$PORT" ask --via-host "What is a Game Boy?"
```

`repl` types straight through to the console. `show` displays a string you already have. Command details and BLE UUIDs are in [docs/PROTOCOL.md](docs/PROTOCOL.md).

### Browser companion

The published page is https://modretrochromatic.grok.me. `web/` is the same companion, plus a local Grok Bot list. It calls xAI from the server (chat completions, model `grok-4.7`) and writes the reply with `grok show` over Web Serial at 115200. The API key stays in `web/.env` (`XAI_API_KEY`). Copy `web/.env.example`. Do not commit the filled-in file.

```bash
cd web
npm install
npm run dev
```

Open http://127.0.0.1:3000 in desktop Chrome or Edge, click Connect, pick a bot, and press Send. Test the link sends `grok show "Chromatic link ok."` without calling xAI. The bot list is stored in this browser only. The four built-in bots cannot be removed. Phone browsers can pick a bot and read the LCD preview. USB connect stays on desktop Chrome or Edge.

The handheld still needs this firmware before `grok show` does anything. Safari cannot open the serial port.

### On the device without the companion

Any 115200 terminal works. Example:

```text
wifi status
grok status
grok ask "Say hello in one sentence"
grok page next
```

A reply looks like this on the console and on BLE notifications:

```text
GROK_BEGIN
Hello from the small screen.
GROK_END
```

## Buttons

The FPGA gives the MCU buttons while the stock menu is open. Start is the shortcut. Select is not visible to the MCU in the stock button driver, so it is not used here. Gameplay buttons are unchanged while the menu is closed.

## BLE

The device advertises as `ChromaticGrok`. Write newline-terminated commands to the RX characteristic and subscribe to TX. The service UUID is `8f3c0001-6b2a-4d5e-9c11-0a1b2c3d4e5f`. RX is `…0002`, TX is `…0003`. There is no pairing. Treat it like the serial port: fine on a trusted bench, not a locked-down link. A line is about 240 bytes, so long `grok show` text should use serial. `companion/requirements-ble.txt` lists `bleak` for anyone writing a central. The Python CLI does not require it.

## Host checks

These do not build or flash the firmware:

```bash
sh tests/run_host.sh
```

That compiles `grok_text` with the host `cc` and checks the proxy's reply parser with no network.

## Known limitations

- This tree was not built with ESP-IDF and was not flashed or tested on a Chromatic here. Build it locally before trusting a flash. NimBLE include paths and Kconfig names follow ESP-IDF v5.3; if `idf.py` disagrees, the build log is the source of truth.
- Light sleep is off by default, so the battery will not last as long as stock.
- The font is ASCII. Other bytes become `?`. Replies are capped at 640 characters, 32 columns, and 12 lines per page.
- One HTTP request at a time. The device speaks plain `http://` only, with no TLS and no API key.
- The proxy has no authentication.
- BLE lines are about 240 bytes. The serial console splits at 32 arguments unless you quote the prompt.
- There is no PSRAM. Wi-Fi and BLE buffers are reduced. AMPDU and HTTPS are off.
- The fourth System row (FIRMWARE, PLAYER #, optional SERIAL #, then GROK) can sit near the battery icon.
- QIO flash mode is not used. Do not turn it on. Those pins belong to the FPGA UART.
- Select cannot be a chord. The stock driver drops it.
- Official recovery through MR Updater installs current stock firmware, which is not the same thing as restoring `chromatic-stock-4mb.bin`.

## Layout

```text
components/grok/     Wi-Fi, BLE, HTTP, screen overlay, commands
companion/proxy.py   LAN proxy. Holds XAI_API_KEY.
companion/chromatic_grok.py
docs/PROTOCOL.md
partitions.csv       NVS stays at 0x9000. Factory app is larger.
```

Stock behavior outside that link is intentionally untouched: FPGA UART, button edges, the settings namespace, and the menu bitmaps.
