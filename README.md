# chromatic-grok

Custom MCU firmware for the ModRetro Chromatic. The stock menu and game path stay in place. This tree adds a Wi-Fi station, a BLE peripheral, and a short text link to Grok.

This is a modified version of the ModRetro Chromatic MCU firmware, distributed under GPL-3.0-only (upstream is GPL-3.0; new files are GPL-3.0-or-later):

- Upstream: https://github.com/ModRetro/oss-chromatic-console-mcu
- Base commit: `63ed3b62b16768ad2415c9e3b865b438ca40fcc6` ("Publish v4.6")
- License: [LICENSE](LICENSE). See [NOTICE](NOTICE).

The Chromatic MCU is an ESP32-U4WDH with 4 MB of flash and no PSRAM. The screen is 160×144. The ESP32 is not on USB directly. The FPGA's composite USB device includes a CDC serial port (USB VID:PID `0x374E:0x0101`, "Chromatic - Player 01") that is bridged to ESP32 UART0, and that port is used for flashing and the `mcu>` console. GPIO9 and GPIO10 are the FPGA UART, so the flash stays in DIO mode.

App version: `0.17.1-grok`. The version string shown in the system menu is `4.6g`. The FPGA does not check that string.

Shiny Relay features (link cable over Wi-Fi, cart flashing, and the rest) are not in this firmware.

## Disclaimer

- **Unofficial.** Not affiliated with, endorsed by, or sponsored by ModRetro or Nintendo. ModRetro and Chromatic are trademarks of ModRetro; Game Boy is a trademark of Nintendo.
- **Flash at your own risk.** Custom firmware can leave the handheld unbootable. Back up the full 4 MB flash first (section 1) and keep that file. Wireless updates (`grok update`) only install signed images and roll back in the app, but recovery from a bad state is over USB. See [docs/REINSTALL.md](docs/REINSTALL.md).
- **Wi-Fi and BLE are for personal experimentation only.** Turning on the ESP32 radios may fall outside the device's certification (FCC/IC/CE or others). Do not ship or sell devices running this firmware.
- **Battery life is shorter** while the Grok link keeps the radios awake (the default). `grok sleep on` restores stock light sleep.
- No warranty. See sections 15 and 16 of the GPL in LICENSE.

## What you get

- Stock OSD, with a GROK row on the System tab and a Start shortcut while the menu is open.
- Wi-Fi station mode. SSID and password are stored in NVS (`grokcfg`) and can be set from the serial console.
- BLE peripheral named `ChromaticGrok`. It speaks the same line commands as the console, but only after passkey pairing (encrypted, bonded, MITM-protected).
- `grok ask` posts the prompt to an `http://` proxy you run (the proxy holds the xAI key), or, in direct mode, calls api.x.ai over HTTPS with BLE off.
- `grok show` displays text the computer already has, so a reply can reach the screen with no device Wi-Fi.
- Replies use the 8x8 `unscii_8` font, 20 columns, 13 lines per page.

## 1. Back up the stock flash

Do this before the first custom flash. The backup is the exact image you can write back later.

Connect the Chromatic over USB and find the port (`/dev/cu.usbmodem…` on macOS, `/dev/ttyACM0` on Linux, `COMx` on Windows). The right one is USB VID:PID `0x374E:0x0101` ("Chromatic - Player 01"). On Linux, `lsusb` shows `374e:0101` and `ls -l /dev/serial/by-id/` maps it to a `ttyACM` node. On macOS, System Information → USB lists it. On Windows, Device Manager shows `VID_374E&PID_0101` under the port's Hardware Ids. The other USB device on the same cable (`0x33AA:0x0120`) is the FPGA's JTAG bridge, not the ESP32.

### Serial speed: 115200 only

That port is the FPGA's USB-to-UART bridge, and it only runs at **115200 baud**. Every `esptool.py` command in this README passes `-b 115200`. Do not use `-b 460800` or any faster rate, and note that `idf.py` defaults to 460800 for flashing unless you pass `-b 115200`. The bridge also delivers the ESP32 ROM boot output in small pieces, so if esptool fails to sync ("Failed to connect" / "Invalid head of packet"), unplug, reconnect, and retry at 115200 rather than raising the speed. Background: the Shiny Relay installer teardown found the bridge runs 115200 only and needed read-path fixes in esptool-js for the fragmented boot banner.

From an ESP-IDF v5.3 shell:

```bash
. $HOME/esp-idf/export.sh
esptool.py --chip esp32 -p "$PORT" -b 115200 read_flash 0x0 0x400000 chromatic-stock-4mb.bin
```

At 115200 a full 4 MB read takes several minutes (roughly 6). Let it finish, keep the Chromatic powered and charged, and do not unplug it mid-read.

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

Back up first (section 1). Then build:

```bash
. $HOME/esp-idf/export.sh
cd ~/Projects/chromatic-grok
idf.py set-target esp32
idf.py build
```

`idf.py set-target esp32` once is enough. The project name is `chromatic_grok`, so the app binary is `build/chromatic_grok.bin`, not `mcu_fw.bin`. The partition table binary is `build/partition_table/partition-table.bin`.

### The first flash must write the partition table too

Stock firmware uses ESP-IDF's default single-app partition table: NVS at `0x9000`, phy_init at `0xf000`, and a **1 MB** factory app at `0x10000`. (Upstream oss-chromatic-console-mcu has no `partitions.csv` and does not set a partition table option, so it gets that default.) This firmware's `partitions.csv` keeps NVS and phy_init in the same places and replaces the 1 MB factory slot with **two OTA app slots of `0x1F0000`** (about 2 MB each), because Wi-Fi, BLE and TLS do not fit in 1 MB:

| Name | Offset | Size |
|---|---|---|
| nvs | `0x9000` | `0x6000` |
| phy_init | `0xf000` | `0x1000` |
| ota_0 | `0x10000` | `0x1F0000` |
| ota_1 | `0x200000` | `0x1F0000` |
| otadata | `0x3F0000` | `0x2000` |

The app still goes to `0x10000` (ota_0). With otadata erased (all `0xFF`) and no factory partition, the ESP-IDF bootloader boots ota_0. `grok update` writes the inactive slot and switches otadata (see [Wireless updates](#wireless-updates-grok-update)).

So the **first** flash of this firmware onto a stock Chromatic (or onto an earlier build with the single `factory` slot) must write:

- the custom partition table at `0x8000`,
- the app at `0x10000`, and
- `build/ota_data_initial.bin` (8 KB of `0xFF`) at `0x3F0000`, so otadata is blank.

Writing only the app at `0x10000` onto a stock device does not work: the image is larger than the stock 1 MB slot, and the stock table at `0x8000` still describes that 1 MB slot, so the app overflows it and will not boot.

Recommended command (writes exactly `0x8000`, `0x10000` and `0x3F0000`, nothing else):

```bash
. $HOME/esp-idf/export.sh
esptool.py --chip esp32 -p "$PORT" -b 115200 write_flash \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/chromatic_grok.bin \
  0x3F0000 build/ota_data_initial.bin
```

Then open the console (115200 is also the monitor speed):

```bash
idf.py -p "$PORT" monitor
```

Leave the monitor with `Ctrl-]`.

### What `idf.py flash` writes

`idf.py -p "$PORT" flash` writes four regions for this project:

| Offset | File | Written by `idf.py flash` | Written by the esptool command above |
|---|---|---|---|
| `0x1000` | `build/bootloader/bootloader.bin` | yes | no |
| `0x8000` | `build/partition_table/partition-table.bin` | yes | yes |
| `0x10000` | `build/chromatic_grok.bin` | yes | yes |
| `0x3F0000` | `build/ota_data_initial.bin` | yes | yes |

The esptool command above is preferred. Rewriting the bootloader at `0x1000` is not needed (the stock bootloader reads the new table and boots the new app), and a bad or interrupted bootloader write is the riskiest part of a flash. Also, `idf.py` flashes at 460800 by default, which this port does not support. If you use `idf.py flash` anyway, pass the speed explicitly: `idf.py -p "$PORT" -b 115200 flash monitor`. That still rewrites `0x1000`.

Neither command touches NVS (`0x9000`) or phy_init (`0xf000`), so menu settings, Wi-Fi, the proxy token and the xAI key survive. A full chip erase (`idf.py erase-flash`) will not. An app-only USB reflash must still write **both** `0x10000 build/chromatic_grok.bin` and `0x3F0000 build/ota_data_initial.bin`: after a wireless update the device may boot ota_1, and only blank otadata makes it boot the app at `0x10000` again. The USB image does not need a signature.

If `idf.py` rejects a name in `sdkconfig.defaults`, fix that symbol from the v5.3 Kconfig instead of guessing. Wi-Fi and BLE together are tight on this chip: buffers are reduced and AMPDU is off. HTTPS (direct mode) only fits with BLE stopped.

## 4. Restore stock firmware

Write the full 4 MB backup back at `0x0`:

```bash
. $HOME/esp-idf/export.sh
esptool.py --chip esp32 -p "$PORT" -b 115200 write_flash 0x0 chromatic-stock-4mb.bin
```

Use 115200 here too; it is the only speed the port supports. Writing 4 MB at that speed takes several minutes. Keep the Chromatic powered and connected until esptool reports the hash was verified.

Restore the whole image at `0x0`, not just the stock app. This firmware replaced the stock partition table at `0x8000`, so writing only a stock app at `0x10000` would leave this firmware's two-slot table on the device, which is not stock. The full backup puts back the bootloader, the stock 1 MB table, NVS and the stock app in one write.

The official MR Updater can also return a Chromatic to current stock firmware: https://support.modretro.com/en_us/chromatic-firmware-updater-ryhoYnzCx (downloads: https://modretro.com/pages/downloads). That updater installs ModRetro's latest stock image. It does not restore your backup file.

Step-by-step reinstall, backup restore, back-to-stock, and key backup: [docs/REINSTALL.md](docs/REINSTALL.md).

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
export GROK_PROXY_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(24))')"
echo "$GROK_PROXY_TOKEN"   # you will type this into the Chromatic once
export GROK_PROXY_ALLOW_LAN=1   # the handheld is on the LAN, so opt in
python3 companion/proxy.py
```

The proxy binds `127.0.0.1:8787` by default. The handheld needs a LAN address, so set `GROK_PROXY_ALLOW_LAN=1` (it then binds `0.0.0.0`, or the `--host` / `GROK_PROXY_HOST` you give). Without that variable a non-loopback `--host` is refused.

Every `POST /v1/ask` needs `Authorization: Bearer <GROK_PROXY_TOKEN>` (at least 16 characters). The token is checked in constant time; anything else gets `401`. The proxy will not start without a token. The xAI key is never sent to a client or written to the log, and key-shaped strings in upstream errors are redacted. Traffic is still plain HTTP, so use a LAN you trust. `GET /health` returns `{"ok": true}`. Copy `companion/.env.example` if you want the variable names in one place. Do not commit a filled-in `.env`.

Find this computer's LAN address, then on the Chromatic:

```text
grok url set http://192.168.1.20:8787/v1/ask
grok token set <the GROK_PROXY_TOKEN value>
grok ask What is a Game Boy?
```

The token is stored in NVS (`grokcfg/token`) and is never printed; `grok status` shows only `token=set` or `token=unset`. `grok token clear` removes it.

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

The device advertises as `ChromaticGrok`. Write newline-terminated commands to the RX characteristic and subscribe to TX. The service UUID is `8f3c0001-6b2a-4d5e-9c11-0a1b2c3d4e5f`. RX is `…0002`, TX is `…0003`.

Pairing is required. The Chromatic pairs as a display-only device using LE Secure Connections with passkey entry: when a central connects, a random 6-digit passkey appears on the handheld screen and you type it on the phone or computer. The bond is stored (up to 3). RX writes need an encrypted, authenticated link, TX reads need the same, and notifications are only sent on such a link. An unpaired central gets "insufficient authentication" and no data. `grok ble unpair` (serial console) clears every bond. A line is about 240 bytes, so long `grok show` text should use serial. `companion/requirements-ble.txt` lists `bleak` for anyone writing a central. The Python CLI does not require it.

## Host checks

These do not build or flash the firmware:

```bash
sh tests/run_host.sh
```

That compiles `grok_text` with the host `cc` and checks the proxy's reply parser with no network.

### Preset questions on the handheld

Open the Grok screen (A on the System tab GROK row, or Start while the menu is open). It starts on a list of 8 questions. **Up/Down** picks, **A** asks, **B** goes back. On the reply, Up/Down pages, A asks again, B returns to the list, Start closes.

The 8 built-in questions are in flash. You can replace any slot; custom text is saved in NVS (`grokcfg/q1`..`q8`) and marked `*` in the list:

```text
grok q list
grok q set 3 "What should I play tonight?"
grok q clear 3          (back to the built-in question)
grok q ask 3            (same as picking it on the handheld)
```

### Direct mode (no proxy, BLE off)

The device can call `https://api.x.ai/v1/responses` itself (model `grok-4.7`, same request as `companion/proxy.py`). TLS only fits in RAM with Bluetooth stopped, so direct mode does not start BLE and gives its controller RAM to the heap. Measured on a Chromatic: about 84 KB internal heap free after BLE stops, and a low point of about 10.4 KB during the call.

```text
grok mode direct
grok key set <xAI API key>     (echoes as ****)
grok status                    (shows xai_key=set, never the key)
grok mem
grok ask "hello"
```

`grok mode proxy` and a reboot bring BLE back. `grok key clear` erases the key. The key is stored **unencrypted** in NVS (`grokcfg/xaikey`); anyone with the device and esptool can read it. Use a low-limit key you can revoke.

### Phone typing (`grok web`)

Type a question on your phone; the answer shows on the Chromatic screen (same overlay as `grok ask`) and on the phone. Off by default.

```text
grok web on        (saved in NVS grokcfg/web; starts once Wi-Fi has an IP)
grok web status    (http://<ip> and the 6-digit PIN)
grok web off
```

At every start the Chromatic makes a new random 6-digit PIN and shows it with its address on the Grok screen (`grok web status` prints it too). On a phone on the same Wi-Fi, open `http://<ip>`, type the PIN and a question (1 to 200 characters, sent as printable ASCII), press Send. The page polls until the answer arrives. The question goes on the same queue as `grok ask`, so direct and proxy mode behave as usual (direct mode still stops BLE). One question at a time; a second one gets "busy" (409). 5 wrong PINs lock the page for 60 s.

- **Plain HTTP.** The PIN, the question and the answer cross your LAN unencrypted. Use it only on a network you trust. The xAI key and proxy token are never sent to the phone.
- Port 80, at most 2 open connections. The server and its buffers (about 8 KB of heap) exist only while it runs.
- Direct mode: the page starts only after BLE has stopped (at boot in direct mode), and the server is stopped during each TLS call and restarted afterwards with the same PIN; the phone keeps polling and gets the answer when it is back. Proxy mode: it runs alongside BLE.
- `grok mem` adds a `mem web:` line (running/paused/stopped, buffer size, server stack and the lowest free stack seen).

### Wireless updates (`grok update`)

```text
grok update                    (latest GitHub release asset chromatic_grok.signed.bin)
grok update https://...        (any https URL, at most 200 characters)
grok update status             (running slot, version, other slot, PENDING/VALID, last result, key id)
grok update rollback           (boot the other slot now, if it holds a valid image)
grok update hold               (test hook: the next update will not confirm and rolls back)
```

The default URL is
`https://github.com/pileofflapjacks1/chromatic-grok/releases/latest/download/chromatic_grok.signed.bin`,
so a release needs an asset named exactly **`chromatic_grok.signed.bin`**. "Latest" skips pre-releases.

What happens: BLE stops (as in direct mode; it comes back after the reboot), the image streams over TLS (CA bundle; GitHub's redirect to release-assets.githubusercontent.com is followed) into the inactive slot in 2 KB pieces, erasing one sector at a time. The first bytes must be a `chromatic_grok` app image. `esp_ota_end()` then checks the image hash and its ECDSA P-256 signature against the public key built into the running firmware. Only if that passes is the boot slot switched and the chip restarted. A rejected or broken download leaves the running firmware and the boot slot alone.

**Signature scheme.** ESP-IDF "signed apps without hardware secure boot" (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`, ECDSA scheme, `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`), with build-time signing off and only the public key (`CONFIG_SECURE_BOOT_VERIFICATION_KEY`, raw 64 bytes) in the build. Checked in the ESP-IDF v5.3.1 source: the app side compiles only `secure_boot_v1/secure_boot_signatures_app.c` (mbedTLS ECDSA over the embedded key, no eFuse access); the eFuse-burning `secure_boot.c` is bootloader-only and only with hardware secure boot enabled; `SECURE_SIGNED_ON_BOOT` is off, so nothing about the bootloader changes and the device's bootloader is never reflashed. A USB flash needs no signature.

**Rollback.** The stock bootloader is not built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, so it ignores the "pending verify" state. Rollback lives in the app (NVS namespace `grokupd`):
- The new image boots as a trial. It is confirmed once Wi-Fi has an IP and the console is running.
- No confirmation within 90 s: the app switches back to the previous slot and restarts.
- A trial that resets before confirming counts a boot; the 3rd unconfirmed boot switches back.
- Going back to a USB-flashed (unsigned) ota_0 resets otadata, which makes the stock bootloader boot ota_0.
- If the new image is not a valid app, the bootloader itself falls back to the other slot and the app records "did not boot".
- **Not covered:** an image that crashes or hangs *before* the boot check at the start of `app_main` (it reboot-loops or hangs without counting), or one that hangs later with no watchdog reset. Recovery is then a USB flash ([docs/REINSTALL.md](docs/REINSTALL.md)). The signature check means only images the key holder signed can get that far.

**Signing a release** (Windows, ESP-IDF 5.3 command prompt, on the key holder's PC; the private key never leaves it):

```bat
rem once: create the key (back it up in a password manager)
espsecure.py generate_signing_key --version 1 chromatic_signing_key.pem
rem once: print the PUBLIC key to paste into the repo (keys/)
python -c "from cryptography.hazmat.primitives import serialization as s;k=s.load_pem_private_key(open('chromatic_signing_key.pem','rb').read(),None);print(k.public_key().public_bytes(s.Encoding.PEM,s.PublicFormat.SubjectPublicKeyInfo).decode())"
rem each release: sign the built app, then upload chromatic_grok.signed.bin as the release asset
espsecure.py sign_data --version 1 --keyfile chromatic_signing_key.pem --output chromatic_grok.signed.bin chromatic_grok.bin
```

The firmware trusts exactly one key: Joe's release key, `keys/joe_signing.pub.pem` (embedded as `keys/joe_signature_verification_key.bin`, made with `tools/pubkey_pem_to_bin.py`). `grok update status` prints its id `13b86339c36256da` (start of the SHA-256 of the DER public key) and "(Joe release key)". A device trusts only the key built into the firmware it runs, so a build with a new key must be flashed over USB once.

## Known limitations

- This tree builds and links with ESP-IDF v5.3 (IRAM 14.3 KB free, static DRAM 11.3 KB free, app 26% free), but it has not been flashed or tested on a Chromatic here. Build it locally before trusting a flash. NimBLE include paths and Kconfig names follow ESP-IDF v5.3; if `idf.py` disagrees, the build log is the source of truth.
- Light sleep is off by default, so the battery will not last as long as stock.
- The font is ASCII. Other bytes become `?`. Replies are capped at 640 characters, 20 columns, and 13 lines per page.
- Phone typing (`grok web`) is plain HTTP with a 6-digit PIN. Anyone on the LAN can read the PIN and questions in transit. It has not been tested on a Chromatic yet.
- One request at a time. Proxy mode speaks plain `http://`. Direct mode uses TLS to api.x.ai with BLE off; the xAI key is then on the device, unencrypted.
- The proxy uses a shared bearer token over plain HTTP. Anyone who can sniff the LAN can see the token; rotate it if the network is shared.
- BLE pairing was written against the NimBLE v5.3 API and has not been tested with a real central here.
- BLE lines are about 240 bytes. The serial console splits at 32 arguments unless you quote the prompt.
- There is no PSRAM. Wi-Fi and BLE buffers are reduced and AMPDU is off. HTTPS only fits with BLE stopped (direct mode).
- The serial console is a minimal line reader (Backspace, Ctrl-U, Ctrl-C, Up arrow for the last command). No TAB completion or longer history.
- Wireless-update rollback is done by the app, not the bootloader (the stock bootloader has no rollback support). An image that crashes before the check at the start of `app_main` is not caught; see [Wireless updates](#wireless-updates-grok-update).
- The fourth System row (FIRMWARE, PLAYER #, optional SERIAL #, then GROK) can sit near the battery icon.
- QIO flash mode is not used. Do not turn it on. Those pins belong to the FPGA UART.
- Select cannot be a chord. The stock driver drops it.
- Official recovery through MR Updater installs current stock firmware, which is not the same thing as restoring `chromatic-stock-4mb.bin`.

## Layout

```text
components/grok/     Wi-Fi, BLE, HTTP/HTTPS, serial console, screen overlay, commands
companion/proxy.py   LAN proxy. Holds XAI_API_KEY.
companion/chromatic_grok.py
docs/PROTOCOL.md
docs/REINSTALL.md    USB reinstall, backup restore, back to stock, signing-key backup
keys/                Joe's release public key only (PEM + raw 64-byte form)
tools/pubkey_pem_to_bin.py  Public PEM -> raw key for CONFIG_SECURE_BOOT_VERIFICATION_KEY
partitions.csv       NVS stays at 0x9000. Two OTA app slots of 0x1F0000 (stock: one 1 MB factory app).
```

Stock behavior outside that link is intentionally untouched: FPGA UART, button edges, the settings namespace, and the menu bitmaps.
