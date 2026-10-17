# Browser installer for the Chromatic ESP32 (draft)

A static page that backs up, installs and restores the Chromatic's ESP32 firmware over Web Serial. Plain HTML and ES modules: no build step, no npm install. It is separate from the TanStack companion in `web/src`.

**Untested on hardware.** Nothing here has run against a real Chromatic. Not deployed anywhere.

## Run it locally

Web Serial needs a secure context, and `http://localhost` / `127.0.0.1` counts as one.

```bash
cd web/flasher
python3 -m http.server 8080 --bind 127.0.0.1   # or: npm run serve
# open http://127.0.0.1:8080/ in desktop Chrome or Edge
```

Other browsers, phones and tablets get "Open this page in Chrome or Edge on a computer".

To offer an install, put a build in `firmware/` (git-ignored):

```bash
. $HOME/esp-idf/export.sh && idf.py build
node web/flasher/tools/make-manifest.mjs --build build --out web/flasher/firmware --commit "$(git rev-parse HEAD)"
```

`make-manifest.mjs` copies `build/partition_table/partition-table.bin` and `build/chromatic_grok.bin` and writes `manifest.json` (commit SHA and link, each file's offset, size and SHA-256). It publishes nothing. With no manifest, the page backs up and restores but has nothing to install.

## Tests

```bash
cd web/flasher && node --test test/*.test.js     # or: npm test
```

Fake ports and fake data only. The transport tests drive the vendored esptool-js `ESPLoader` against a fake ROM that answers in 1–31 byte chunks after boot text.

## Flow

Connect → Save my backup (read 4 MB, checked automatically) → Confirm your backup saved (pick the file) → Install → Done. "Undo: put my Chromatic back the way it was." opens Restore. Everything technical is under "Technical details".

## Safety model

| Rule | Where |
| --- | --- |
| Port chooser shows only USB 0x374E:0x0101 (ESP32 serial). 0x33AA:0x0120, the JTAG bridge, is refused by name. | `src/device.js` |
| 115200 baud only, never a baud change. ClassicReset, with DTR and RTS set in one `setSignals()` call every time (Windows usbser.sys). EN = not RTS, IO0 low only when DTR=1, RTS=0. | `src/serial.js`, `src/lib/signals.js` |
| Our own SLIP reassembly, so packets split into 1–31 byte pieces still arrive whole. One reader, stopped cleanly (cancel once, wait, release) before every reset and disconnect; `flushInput` no longer cancels the reader. | `src/lib/slip.js`, `src/lib/pump.js` |
| Chip must be ESP32 and flash must report 4 MB, or nothing proceeds. Stub loaded before any MD5. | `src/lib/identity.js` |
| Backup = one `chromatic-backup-<date>.chromatic` file: header (MAC, size, SHA-256…) + raw 4 MB. Format in `src/lib/container.js`. Advanced: export a raw esptool-compatible `.bin`. | `src/lib/container.js` |
| Backup counts only if on-chip MD5 of 0x0–0x400000 equals the MD5 of the read bytes, 0xE9 is at 0x1000, 0xAA50 is at 0x8000, and the user re-picks the saved file and its SHA-256 matches. | `src/lib/gating.js` |
| Install unlocks only after the 3 checks AND that verified backup. | `src/lib/gating.js` |
| Install writes only 0x8000 (≤ 0x1000) and 0x10000 (≤ 0x1F0000), each SHA-256 checked against the manifest first. Never below 0x8000, never 0x1000, no full erase, no eFuse writes. flashMode/Freq/Size `keep`, compressed, then on-chip MD5 per region, then hard reset. | `src/lib/regions.js`, `src/lib/manifest.js` |
| Restore refuses unless the chip reports exactly 4 MB, the file's magic and version are right, the image is 4,194,304 bytes, its SHA-256 matches the header, the header MAC equals the chip's MAC, and the image passes the 0x1000/0x8000 checks. Then writes at 0x0 and checks MD5. | `src/lib/restore.js` |
| A failed MD5 after writing shows "Don't unplug" and points to Restore. The port stays open. | `src/lib/errors.js` |

## Not yet tested on hardware

- Whether the stock bootloader boots this build.
- Whether the SLIP reassembly works on the real bridge (tested only against a fake that splits replies into 1–31 byte chunks).
- Whether Chrome keeps bytes that arrive while the reader is stopped during a reset (the boot banner); the code doesn't depend on it.
- Whether the stub's on-chip MD5 works through the bridge at 115200. The plan's fallback (a second full read) is not built.
- Read and write speed at 115200 (the plan estimates about 6 minutes for 4 MB).
- What happens when the game chip's image isn't running (no ESP32 serial port at all).

## Third-party code

`vendor/esptool-js-0.5.7/bundle.js` is the unmodified npm `esptool-js@0.5.7` bundle (Apache-2.0). See `vendor/esptool-js-0.5.7/LICENSE` and `NOTICE.md`.
