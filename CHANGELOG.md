## 0.18.0-grok (draft, unreleased)

### Added
- Per-bot last prompt: each ask (serial, BLE, phone typing, retry) saves the prompt and a short reply snippet in NVS (`grokcfg/b0`..`b3`, capped ~80 + ~40 chars). The bot detail screen shows "Where we left off:" with that text, or "(nothing yet)".
- Bot list ends with a **Phone typing** row that turns on `grok web` and shows the PIN / URL. Phone typing no longer steals the overlay when Wi-Fi connects.

### Changed
- Opening the Chromatic menu (OSD) goes to the Grok loader, then the bot list. Start / System GROK A still do the same.
- Removed the on-device preset question picker from the bot flow. Serial `grok q list|set|clear|ask` and the built-in prompts stay for the console.
- App version `0.18.0-grok`. Menu firmware string stays `4.6g`. Partition table, bootloader and signing key unchanged.

### Fixed
- `grok_web_kick` no longer opens the PHONE TYPING overlay by itself (that was covering the bot menu whenever Wi-Fi came up with web previously enabled).

## 0.17.1-grok (draft, unreleased)

### Fixed
- Opening Grok always plays the connecting loader for at least one full animation cycle (~2.19 s from `grok_loader_frames`), even when Wi-Fi is already connected or no SSID is set. After that cycle, the bot menu opens once Wi-Fi is up or there is no SSID; the existing 12 s give-up still applies while connecting.

### Changed
- App version `0.17.1-grok`. Menu firmware string stays `4.6g`. Partition table, bootloader and signing key unchanged.

## 0.17.0-grok (draft, unreleased)

### Added
- Loading screen: Carty G's v2 160×144 ten-frame Grok Bot art (white circle + slanted eyes, 3-color palette + RLE, ~4.2 KB flash) plays while Wi-Fi connects. Source PNGs in `components/grok/loader/`; `tools/pack_loader_frames.py` regenerates the embedded frames. Painted with LVGL rect runs (no RGB565 scratch buffer).
- On-device bot/team menu (D-pad): Carty G, The Chief, Awesome Coder, Chromatic Add-ons. Selecting one shows a "where we left off" placeholder (empty note for now). No Grok Bot network API.
- `grok bots`: lists the static bots on the console and opens the menu.
- Host tests: `tests/test_grok_bots.c`, `tests/test_grok_loader.c`.

### Changed
- App version `0.17.0-grok`. Menu firmware string stays `4.6g`. Partition table, bootloader and signing key unchanged.
- Start / System GROK A opens the loader (if Wi-Fi is still connecting) or the bot menu instead of the question picker. A on a bot note still reaches the question picker.

## 0.16.1-grok (draft, unreleased)

### Changed
- Question picker rows show a short label for each built-in question (at most 17 characters, so `>1*` plus the label fits the 20-column row). The full question is still what is asked and is still shown in the preview under the list and in `grok q list`. Custom questions are unchanged.
- App version `0.16.1-grok`.

### Added
- `tests/test_grok_text.c`: every short label fits the row and every preset keeps its full question.

## 0.16.0-grok (draft, unreleased)

### Added
- Phone typing: `grok web on|off|status` (NVS `grokcfg/web`, off by default). A small HTTP server (`esp_http_server`, port 80, at most 2 sockets, 4 KB task stack) serves one inline page: PIN, question, Send. `POST /ask` queues the question on the same job queue as `grok ask` (direct or proxy mode); `GET /answer` returns `idle`/`wait`/`done` and the answer. The reply also shows on the Chromatic screen.
- A random 6-digit PIN (`esp_random`) at every server start, shown on the Grok screen and in `grok web status`. Constant-time compare; 5 wrong PINs lock the page for 60 s. Busy returns 409. Questions are capped at 200 characters and folded to printable ASCII.
- `grok mem` prints a `mem web:` line.
- `tests/test_grok_web.c`: host tests for the PIN check, lockout, form parsing and sanitizing.

### Changed
- App version `0.16.0-grok`. Partition table, bootloader and signing key unchanged.
- `CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024` (phone browsers send long header lines).
- Direct mode and `grok update` stop the phone page's server for the TLS call and restart it afterwards (same PIN). In direct mode the page starts only after BLE has released its RAM.

### Security
- Plain HTTP on the LAN: the PIN, question and answer are not encrypted. The xAI key and proxy token are never sent to the phone.

## 0.15.0-grok

### Added
- `grok mode direct|proxy`: direct mode calls `https://api.x.ai/v1/responses` from the device (model `grok-4.7`) with BLE stopped and its controller RAM released. Proxy mode is unchanged.
- `grok key set <key>` / `grok key clear`: xAI key in its own NVS slot (`grokcfg/xaikey`), separate from the proxy token. Stored unencrypted.
- `grok mem`: internal and DMA heap free / largest block / minimum, and the calling task's free stack.
- Preset question picker on the Grok screen: Up/Down picks, A asks, B goes back. 8 built-in questions in flash; `grok q list`, `grok q set <n> <text>`, `grok q clear <n>`, `grok q ask <n>` (custom text in NVS `grokcfg/q1`..`q8`).
- Two-slot OTA partition layout (ota_0 `0x10000`, ota_1 `0x200000`, both `0x1F0000`; otadata `0x3F0000`).
- `grok update [url]`: wireless update into the inactive slot over HTTPS (default: latest GitHub release asset `chromatic_grok.signed.bin`). Images must carry an ECDSA P-256 signature (`espsecure.py sign_data --version 1`) for the public key built into the firmware; ESP-IDF "signed apps without secure boot", app-side check only, stock bootloader and eFuses untouched.
- App-level rollback: a new image runs as a trial and is confirmed once Wi-Fi and the console are up; otherwise it switches back after 90 s or on the 3rd unconfirmed boot. `grok update status`, `grok update rollback`, `grok update hold` (test hook).
- `docs/REINSTALL.md`: USB reinstall, backup restore, back to ModRetro firmware, signing-key backup.
- `keys/joe_signing.pub.pem`: the only trusted update key (Joe's release key, id `13b86339c36256da`). Public keys only in git. `tools/pubkey_pem_to_bin.py` converts a public PEM to the raw form the build embeds.
- Browser companion in `web/`. It asks xAI from the server and sends `grok show` over USB. A local, editable Grok Bot list picks the voice for the next question.

### Changed
- App version `0.15.0-grok` (`fwversion`). Menu firmware string stays `4.6g`.
- Grok reply overlay uses the 8x8 `unscii_8` font (20 columns) so text is legible.
- The serial console is a small line reader instead of the linenoise REPL. Secrets typed after `grok key set`, `grok token set` and `wifi set <ssid>` echo as `*` and are never kept for Up-arrow recall. No TAB completion or multi-entry history.
- README flashing docs: every esptool command uses `-b 115200` (the FPGA's USB CDC bridge, `0x374E:0x0101`, runs 115200 only). The first flash writes the partition table at `0x8000`, the app at `0x10000` and blank otadata at `0x3F0000` with an explicit esptool command; `idf.py flash` also rewrites the bootloader at `0x1000`. Restore writes the full 4 MB backup at `0x0`.

### Fixed
- `ble: adv fields failed (30)` when direct mode stopped BLE: advertising is no longer restarted while the host is stopping.

## 0.14.0-grok

Modified version of the ModRetro Chromatic MCU firmware at `63ed3b62b16768ad2415c9e3b865b438ca40fcc6` (v4.6). GPL-3.0-or-later. See NOTICE.

### Added
- Wi-Fi station mode. Credentials live in the `grokcfg` NVS namespace and are set from the serial console.
- BLE peripheral `ChromaticGrok` with the same line protocol as the `mcu>` console.
- Grok link: `grok ask` posts to a configurable `http://` proxy, and `grok show` displays a reply sent by a companion. Short replies use the stock font on the 160×144 screen.
- Python proxy and serial companion in `companion/`.
- System-menu row GROK, and Start while the menu is open, to read and page a reply.

### Changed
- App version `0.14.0-grok`. Menu firmware string `4.6g` (display only).
- Custom partition table. The factory app is `0x1F0000` bytes. NVS stays at `0x9000` / `0x6000`.
- Light sleep stays off unless `grok sleep on`. Wi-Fi and BLE cannot survive the stock light sleep.
- Project name is `chromatic_grok`.

## v0.13.8

### Changed
- Updated FPGA version number

## v0.13.7

### Changed
- Change default value for usbcolorcorrection from 1->0.

## v0.13.5

### Note 
The FPGA must be updated to v18.9 or newer for PCB-aware battery voltage estimate

### Changed 
- Uses most significant bit of 16-bit ADC packet sent from FPGA to determine which
battery voltage calculation to execute.

## v0.13.4

### Note
The FPGA must be updated to v18.7 or newer for fixed sprite palette behavior

### Changed
- Fixed bug where sprite palettes are not set correctly when using palette
selection tab.

## v0.13.3

### Note
The FPGA must be updated to v18.6 or newer for palette selection and updated
frame blending behavior.

### Added
- The ability to change the color palette for non-GBC titles through a Palette tab.

### Changed
- Renamed the frame blending toggle button text to "1:1" from "3:1" to clarify
the updated FPGA approach.
- Added redundancy to two-option menu widgets. Press B to toggle previous choice.
- Added up/down roll over to bottom/top for OSD navigation.

## v0.13.2

### Added
- The ability to mock button to the FPGA.

### Changed
- Simplified the firmware version reported the system tab. Press A to see details.

## v0.13.0

### Added
- Option to ignore diagonal inputs from the D-Pad
- Option to use the background palette 0 color (prevents screen transition flashing)
- Option to set low battery icon display behavior
- Added message to brightness to indicate low power mode is active
- Added hot keys to the Controls tab

### Changed
- Tuned battery voltage thresholds for 1.2V NiMH AA's
- Prevent backlight adjustments when in low power mode
- Renamed 'USB Color Correction' menu option to 'Streaming'
- Redesigned Controls tab, toggle option text

## v0.12.3

### Note
The FPGA must be updated to v17.0 or newer as the underlying communication protocol changed.

## Added
- Sleep the MCU on power up to reduce power consumption.
- Support LiPo battery thresholds and charging indication in the battery menu.

## Fixed
- Highest brightness setting is not properly restored on power up.
- Hot key brightness adjustments are saved between power cycles

### Changed
- Supports updated UART protocol.

## Removed
- The Frame Blending text hint on the Display screen.

## v0.11.2
This is the first production release.
