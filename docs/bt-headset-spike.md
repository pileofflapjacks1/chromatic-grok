# Bluetooth headset spike (HFP Audio Gateway)

One question: can the Chromatic's ESP32 act as a hands-free **Audio Gateway**
for Bluetooth headphones, record the headset mic, play audio back to it, and
still have enough RAM for Wi-Fi and a network request?

This is a test build, not a release. It is off unless you build with the
`sdkconfig.bt_spike` overlay. The normal build (`sdkconfig.defaults` only) is
unchanged: same NimBLE `ChromaticGrok` peripheral, no `bt` command.

**Result on hardware (2026-11-18): no-go.** With Wi-Fi up, Bluetooth
Classic + Bluedroid + HFP AG left about 2 KB of internal heap (low point
904 bytes), and the headset scan then crashed. Nothing is left for SCO, the
audio clip, HTTP or TLS. See [Hardware result](#hardware-result-2026-11-18).

Before hardware the estimate was: TLS with Bluetooth Classic up is a
no-go, and plain HTTP to the LAN proxy is marginal. See
[Memory budget](#memory-budget) and [Go / no-go](#go--no-go).

## Hardware result (2026-11-18)

Joe's Chromatic, spike build compiled on his Windows laptop (ESP-IDF
v5.3.5), flashed over USB at 115200. Direct mode, phone page on (its
server was running during the test), AirPods-class headset not reached.

| `btmem` stage | int8 free | Change |
|---|---|---|
| `boot` (radio task start, before Wi-Fi) | 90,604 B | |
| `mem boot` (Wi-Fi starting) | 52,936 B | |
| `start` / `wifi connected` | 40,716 / 40,652 B | |
| `ble released` | 45,560 B | +4,908 B |
| `controller on` (BR/EDR) | 33,480 B | −12,080 B |
| `bt enabled` (Bluedroid + HFP AG) | **2,048 B** (largest block 800 B, min 904 B) | −31,432 B |
| `bt scan` | **crash** | |

The scan crashed with `Guru Meditation Error: Core 0 panic'ed
(StoreProhibited)`, `EXCVADDR 0x00000000`, PC `0x4000c46c` (a ROM memory
routine): a write through a NULL pointer, consistent with an allocation
failing in the Bluetooth stack at under 1 KB free. The backtrace was not
decoded (the ELF is on the build laptop:
`xtensa-esp32-elf-addr2line -pfiaC -e build-bt\chromatic_grok.elf <addresses>`).
The device rebooted normally; NVS and settings were unaffected.

What it shows:

- Bluetooth Classic costs about **43.5 KB** of heap at runtime (controller
  12.1 KB, Bluedroid + HFP AG 31.4 KB), inside the estimate below.
- Releasing BLE memory gives back 4.9 KB, as estimated.
- The go criterion needs 40 KB *left* with SCO open and a request in
  flight. The measured headroom is about 2 KB before SCO, the 20 KB clip,
  10–15 KB of HTTP or ~74 KB of TLS. Turning the phone page off would
  return a few KB, not 40+.

Conclusion: the ESP32 in this firmware cannot be the headset's Audio
Gateway. The voice feature should pair the headphones with the phone or
computer instead: it records, runs speech-to-text, Grok and
text-to-speech, plays the reply, and sends the text to the Chromatic with
the existing `grok show` (USB or Wi-Fi). The browser companion (`web/`)
fits that role. The device's phone typing page cannot use the microphone,
because browsers only allow it on secure (HTTPS) pages.

To go back to the release firmware, flash `build\` with the same
three-region command. The spike branch stays as a record; it is not meant
to be merged.

## What the spike build changes

- Bluetooth host: **Bluedroid** instead of NimBLE. HFP is Bluetooth Classic,
  which NimBLE does not do, and the two hosts cannot be built together. So
  this build has **no BLE peripheral** (`grok_ble.c` compiles to its stubs;
  `grok status` shows `ble off`). Serial, Wi-Fi, proxy/direct mode, the
  phone page and `grok show` work as usual.
- A `bt` console command (`components/grok/grok_bt.c`), and portable helpers
  (`grok_bt_util.c`, host-tested in `tests/test_grok_bt.c`).
- Kconfig: `CONFIG_GROK_BT_HEADSET_SPIKE` (menu "Chromatic Grok"), default
  `n`, which only shows up when Bluedroid + HFP AG + HCI audio are on.
  `CONFIG_GROK_BT_REC_SECONDS` (default 5).
- Light sleep stays off while the Bluetooth controller is on
  (`Grok_WantsRadioAwake()` also checks `grok_bt_active()`; in the normal
  build that is always false).
- No pins. Audio goes over HCI, not the PCM pins (25/26/27/33 are the FPGA
  I2S). No FPGA link pins, SD pins, `button.c`, `fpga_rx.c`, `gfx.c`, NVS
  layout or settings namespace were touched.

### sdkconfig changes (`sdkconfig.bt_spike`, on top of `sdkconfig.defaults`)

| Option | Release | Spike | Why |
|---|---|---|---|
| `BT_NIMBLE_ENABLED` | y | n | HFP needs Classic; NimBLE is BLE-only |
| `BT_BLUEDROID_ENABLED` | n | y | Bluedroid has the HFP AG profile |
| `BT_CLASSIC_ENABLED` | n | y | BR/EDR |
| `BT_BLE_ENABLED` (Bluedroid's BLE half) | n/a | n | not used; smaller |
| `BTDM_CTRL_MODE_*` | BLE_ONLY | BR_EDR_ONLY | |
| `BTDM_CTRL_BR_EDR_MAX_ACL_CONN` | n/a | 1 | one headset (default 2) |
| `BTDM_CTRL_BR_EDR_MAX_SYNC_CONN` | n/a | 1 | one SCO link (default 0 = none) |
| `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI` | n/a | y | audio in software, no PCM pins |
| `BT_HFP_ENABLE`, `BT_HFP_AG_ENABLE` | n/a | y | Audio Gateway role |
| `BT_HFP_CLIENT_ENABLE` | n/a | n | not the hands-free side |
| `BT_HFP_AUDIO_DATA_PATH_HCI` | n/a | y | |
| `BT_HFP_WBS_ENABLE` | n/a | **n** | CVSD 8 kHz first; flip to measure mSBC 16 kHz |
| `BT_A2DP_ENABLE`, `BT_SPP_ENABLED` | n/a | n | |
| `BT_SSP_ENABLED` | n/a | y | Secure Simple Pairing (Just Works) |
| `BT_BLE_DYNAMIC_ENV_MEMORY` | n/a | y | Bluedroid control blocks on the heap, not .bss |
| `BT_STACK_NO_LOG` | n/a | y | no Bluedroid trace strings |
| `ESP_COEX_SW_COEXIST_ENABLE` | y (default) | y | Wi-Fi/BT time sharing, made explicit |
| `GROK_BT_HEADSET_SPIKE` | n | y | the `bt` command |

Everything else (Wi-Fi buffers, mbedTLS dynamic buffers, `-Os`, partition
table, signing key) is the same as the release build.

## Build

Built and checked here with ESP-IDF v5.3: both configurations compile and
link with no warnings in the grok component.

| | Release (`build/`) | Spike (`build-bt/`) |
|---|---|---|
| App image | 1,572,784 B (23% of slot free) | 1,703,856 B (16% free) |
| Static DRAM (.data + .bss) | 114,484 B | 107,536 B (**−6.9 KB**) |
| IRAM | 115,903 B | 112,850 B |

The spike build uses its own build folder and sdkconfig, so it never
touches the normal `build/` or `sdkconfig`. From an ESP-IDF 5.3 prompt in
the repo:

```bat
idf.py -B build-bt -D SDKCONFIG=build-bt/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.bt_spike" set-target esp32
idf.py -B build-bt -D SDKCONFIG=build-bt/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.bt_spike" build
```

(`set-target` once. On macOS/Linux use `sh`-style quoting:
`-D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.bt_spike"`.)
Check `build-bt/sdkconfig` has `CONFIG_GROK_BT_HEADSET_SPIKE=y` and
`CONFIG_BT_BLUEDROID_ENABLED=y`.

## On the test laptop (Windows, COM4)

### 0. Backup

If this Chromatic has never been backed up, do README section 1 first (full
4 MB `read_flash`, about 6 minutes at 115200). If you already have
`chromatic-stock-4mb.bin`, you are covered: this flash does not touch the
bootloader, partition table layout, NVS or phy_init.

### 1. Get the build

Either:

- **Pull and build there:** `git fetch origin` then
  `git checkout claude/frosty-einstein-a77345`, and run the two `idf.py`
  commands above. The files land in `build-bt\`.
- **Or copy three files** from a machine that built it (`*.bin` is not in
  git): `build-bt\partition_table\partition-table.bin`,
  `build-bt\chromatic_grok.bin`, `build-bt\ota_data_initial.bin`.

### 2. Flash (same three regions as docs/REINSTALL.md)

Close Miniterm and anything else holding COM4 first.

```bat
esptool.py --chip esp32 -p COM4 -b 115200 write_flash 0x8000 build-bt\partition_table\partition-table.bin 0x10000 build-bt\chromatic_grok.bin 0x3F0000 build-bt\ota_data_initial.bin
```

Do not use `idf.py flash` (it rewrites the bootloader at `0x1000` and
defaults to 460800). Do not erase flash. NVS stays, so Wi-Fi, proxy URL,
token and key are still set. Bluedroid saves headset bond keys in NVS (a
few hundred bytes, its own namespace).

### 3. Test

Open Miniterm on COM4 at 115200. Check `wifi status` shows `up` and
`grok url` is your proxy (for the HTTP stage). Put the headset in pairing
mode (usually hold power until the LED flashes red/blue), then:

```text
mcu> bt test
```

That runs every stage on the radio task and prints a `btmem` line at each:

| Stage | What happened |
|---|---|
| `boot` | radio task start, before Wi-Fi (printed at boot) |
| `start`, `wifi connected` | before Bluetooth |
| `ble released` | `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` |
| `controller on` | BR/EDR controller enabled |
| `bt enabled` | Bluedroid + HFP AG up |
| `headset slc` | scanned (10 s), paired, HFP service-level link up |
| `sco open` | SCO audio link open (codec printed: cvsd/msbc) |
| `recording` | halfway through 5 s of mic capture |
| `recorded` | clip in RAM; prints samples, packets, peak level |
| `played` | clip played back to the headset (a 2 s 400 Hz tone plays first) |
| `before/after proxy` | `grok ask`-style HTTP POST to the proxy, SCO still open |
| `before tls`, `tls handshake`, `after tls` | TLS handshake + GET to `https://api.x.ai/v1/models` (no key sent; a 401 is expected), SCO still open |
| `sco closed` | |

Each line looks like:

```text
btmem sco open         total=… | int8 free=… largest=… min=… | dma free=… largest=… min=…
```

`min=` is the **lowest internal heap since the previous line**, so it
covers that stage's worst moment. The go/no-go number is the smallest
`int8 min=` from `sco open` through `after tls`.

Already know the headset address? `bt test aa:bb:cc:dd:ee:ff` skips the
scan. A bonded headset reconnects with `bt pair` (no scan).

Step by step instead, if `bt test` stops somewhere:

```text
bt on            controller + Bluedroid + HFP AG
bt scan          10 s inquiry, lists devices, [audio] marks headsets
bt pair [mac]    pair + HFP link (scan and take the first [audio] device if no mac)
bt sco           open audio (falls back to voice-recognition activation)
bt tone          2 s 400 Hz tone to the headset
bt rec [secs]    record 1-10 s of mic (default 5)
bt play          play it back
bt proxy         HTTP ask to the proxy with BT up
bt tls [url]     TLS probe with BT up
bt sco off       close audio
bt mem           heap now
bt status        state, bonds
bt unpair        forget bonded headsets
bt off           tear down Bluetooth, free the clip
```

For the real reply-size TLS case, `grok mode direct` and `grok ask hi`
while `bt sco` is open also works (it prints its own `mem before/after tls`
lines; the spike build has no NimBLE to stop). Expect it to fail; see
below.

Paste the whole Miniterm log back to Claude.

### 4. Go back to the release firmware

Flash the normal `build\` files with the same three-region command (or a
release's files per docs/REINSTALL.md). NVS is untouched.

## Memory budget

Estimated from the build's size output, the IDF v5.3 controller memory map
(`components/bt/include/esp32/include/esp_bt.h`) and the README's hardware
numbers. Hardware will replace these.

Known (measured on the Chromatic, release build, direct mode): about
**84 KB** internal heap free with Wi-Fi up **after the whole BT controller
region was released**, and a low point of **10.4 KB** during a direct TLS
call. So one TLS call to api.x.ai costs about **74 KB** at peak.

| Item | Effect on free internal heap | Source |
|---|---|---|
| Release, Wi-Fi up, BT region released | 84 KB | measured |
| BT controller region kept (needed for Classic) | −54 KB | 0xdb5c reserved, `mem_release(BTDM)` not possible |
| Spike static DRAM saving | +7 KB | size diff above |
| `mem_release(ESP_BT_MODE_BLE)` in BR/EDR mode | +5 KB | BLE exchange memory 0x3ffb09a8–0x3ffb1ddc |
| **Spike, Wi-Fi up, before `bt on`** | **≈ 42 KB** | estimate |
| Controller task + controller heap | −8 to −12 KB | |
| Bluedroid tasks (BTU 4.3 KB, BTC 3 KB, HCI) | −9 KB | stack sizes in sdkconfig |
| Bluedroid control blocks, L2CAP/RFCOMM/SDP/HFP, buffers | −20 to −30 KB | typical for Classic |
| **After `bt on` + SLC** | **≈ −5 to +5 KB** | estimate |
| SCO buffers | −1 to −2 KB | |
| 5 s clip (ADPCM, CVSD) | −20 KB (freed by `bt off`) | 4 KB/s; raw PCM would be 80 KB |
| Plain HTTP to proxy (socket, lwIP, client buffers) | −10 to −15 KB | |
| TLS handshake + 16 KB in-record | −60 to −74 KB | measured above |

The column does not close. On these estimates Bluedroid with Wi-Fi up
leaves roughly nothing, before any audio buffer or request. If so, `bt on`
or the SLC fails with `no memory`, or the clip allocation fails (`bt rec`
says how much it wanted and the largest free block). That would itself
answer the question for this firmware as it is.

The estimates for Bluedroid are the least certain line (±15 KB). That is
why the spike exists.

## Go / no-go

Measured with the headset's SCO link open, Wi-Fi connected and a request in
flight (smallest `int8 min=` from `sco open` to `after tls`):

- **Go (proxy voice design):** at least **40 KB** internal heap free at the
  low point **with the plain HTTP proxy request** in flight and the 5 s
  clip allocated, no `rx lost`/`tx discarded` growth in the
  `sco packets` line during the request, and the playback sounds right. The
  real design streams audio to the proxy in small chunks instead of a 20 KB
  clip, so this leaves room for that.
- **Go (direct TLS):** the same 40 KB with the TLS probe in flight.
  Not expected.
- **No-go:** `bt on` or pairing fails for memory, the clip cannot be
  allocated, or the low point is under ~15 KB (allocation failures and
  Wi-Fi drops start there on this firmware).

## Risks

**RAM.** As above. Bluetooth Classic keeps ~54 KB of controller memory that
the release firmware hands back in direct mode, and Bluedroid is much
heavier than NimBLE. TLS (74 KB peak) and Bluedroid cannot both fit with
this UI and Wi-Fi config. Levers, in order: no TLS on the device (the proxy
does speech-to-text, Grok and text-to-speech over plain HTTP, which is the
plan anyway); stream audio instead of buffering a clip; smaller Wi-Fi
buffers (`ESP_WIFI_STATIC_RX_BUFFER_NUM` 4→3, dynamic RX/TX 16→8); smaller
Bluedroid task stacks after measuring their high-water marks; turn Bluetooth
off (`bt off`) for any TLS call (direct mode, `grok update`), as the
release does with BLE today.

**Coexistence.** One 2.4 GHz radio is time-shared. SCO reserves slots every
3.75 ms (HV3/EV3), so Wi-Fi throughput drops and latency rises while audio
is open, and Wi-Fi bursts can cost SCO packets (clicks or gaps in the
recording; the `recorded … in N ms` line warns if it took longer than real
time, and `sco packets` counts loss). The spike records and plays before
the network stage so it can tell the two apart; the network stage runs with
SCO open to show the worst case. Keep the access point close for the test.

**Headset HFP quirks.**
- Some headsets refuse audio unless there is a "call" or voice recognition;
  `bt sco` retries with `AT+BVRA` voice-recognition activation.
- Some will not take an AG-initiated connection while in pairing mode on
  other devices, or reconnect only to the last phone. Turn the phone's
  Bluetooth off during the test.
- Older headsets use legacy PIN pairing; the spike answers `0000`.
- mSBC (16 kHz, better for speech recognition) is negotiated only if both
  sides support it and `BT_HFP_WBS_ENABLE=y`; it doubles the clip size and
  adds the SBC codec. Test CVSD first.
- The headset's voice button sends `AT+BVRA=1`; the spike prints
  `headset voice button: on` and opens audio. That is the natural
  push-to-talk trigger for the real feature.
- HFP sounds like a phone call (8 kHz, narrow band). Fine for speech
  recognition and short replies; not for music. Game audio still goes
  FPGA → codec and is not routed to Bluetooth.

**Other.**
- The BLE peripheral is gone in this build. A real feature would need
  Bluedroid's BLE as well (more RAM) or drop BLE.
- `grok update` in the spike build runs TLS with the BT controller
  possibly on. Run `bt off` first, or reflash over USB.
- The spike build is about 130 KB bigger (16% of the OTA slot left).
