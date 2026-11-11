## 0.17.1-voice.5 (draft, unreleased)

### Changed
- Voice screen look: amber header bar with the bot name and a Listening /
  Thinking / Reply status, navy background, cream reply text in a rounded
  cyan-bordered panel, a footer bar with `A next B exit`, the page as `2/4`,
  and up/down triangles that dim at the ends. The ball loader keeps its art
  with voice colors and cyan (listening) or amber (thinking) dots.
- Voice caption page is **18 columns by 11 rows** (was 20 by 13) because the
  border and padding take room. Voice rows are 9 px; the plain overlay's 10 px
  rows are unchanged. Paging, A/Up/Down, B and the DTR/RTS fix are unchanged.
  The plain reply and picker pages are unchanged.
- `tools/voice_preview.py` renders a host preview and prints contrast ratios.
- **UNTESTED ON HARDWARE.** Colors on the Chromatic LCD (with its color
  correction) are not verified.

## 0.17.1-voice.4 (draft, unreleased)

### Fixed
- Voice captions wrap on the 20-column unscii_8 body (13 rows) and A/Up
  move by those wrapped lines. The 640-character cap stays. **UNTESTED ON
  HARDWARE.**

### Host
- `--list-mics` and `--mic`. The default input skips a device whose name
  contains Chromatic. **UNTESTED ON HARDWARE.**

## 0.17.1-voice.3 (draft, unreleased)

### Host
- `voice_ptt.py` and the Python companion open the USB console with DTR and
  RTS set false before `open()`. `requests==2.34.2` is pinned for
  `faster-whisper` 1.1.1 on Python 3.13. The voice.3 firmware image is
  unchanged. **UNTESTED ON HARDWARE** beyond one readiness report from Joe
  via Carty (`VOICE_BOT` / `VOICE_READY`, no freeze). Microphone path not
  verified.

### Fixed
- An open voice frame no longer returns to the console after 700 lines.
  Overlong, malformed, and truncated frames discard reply bytes until a line
  that is exactly `VOICE_END` or `VOICE_ABORT`. `wifi clear` past that old
  cap is not executed. Length fields that do not match the payload fail
  closed the same way. **UNTESTED ON HARDWARE.**

## 0.17.1-voice.2 (draft, unreleased)

### Fixed
- Laptop voice replies are length-prefixed (`VOICE_DATA` / `VOICE_NL`). A body
  line that is `VOICE_BEGIN`, `VOICE_END`, or `wifi clear` is caption text.
  Overlong, malformed, and truncated frames stay out of the console until
  `VOICE_END`, `VOICE_ABORT`, or 700 in-frame lines, then the body is dropped.
- Host chunks stay within the device line cap (240). A 640-character reply fits
  the body buffer. Quotes and backslashes are not rewritten.

### Changed
- App version `0.17.1-voice.2` (superseded by `0.17.1-voice.3`; that image
  closed the frame after 700 lines). The `0.17.1-voice.1` raw body lines are not
  compatible with this parser. **UNTESTED ON HARDWARE.**

## 0.17.1-voice.1 (draft, unreleased)

### Added
- Laptop voice → Chromatic display (USB serial): `voice` console verbs,
  `VOICE_BEGIN`/`VOICE_END` framed replies, listening/thinking states using
  the existing v2 Grok Bot ball loader, and phrase-at-a-time captions.
- D-pad Left/Right on the voice screen cycles the bot-menu list and reports
  `VOICE_BOT <name>` over serial.
- `tools/voice_ptt.py` (Windows-first push-to-talk): local faster-whisper STT,
  xAI via `XAI_API_KEY` env only, framed serial to the Chromatic.
- `tools/requirements-voice.txt` (pinned), `docs/laptop-voice.md`,
  `tests/test_grok_voice.c`.

### Changed
- App version `0.17.1-voice.1` (stacked on `0.17.1-grok` / bot-menu; avoids
  colliding with `0.18.0-grok` and `0.19.0-grok` on other branches). Menu
  string stays `4.6g`.

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
