# Laptop voice → Chromatic display

**UNTESTED ON HARDWARE.**

Stacked on the bot-menu branch (`0.17.1-grok`). App version for this add-on:
`0.17.1-voice.1` (keeps the 0.17.1 bot-menu baseline; `0.18.0-grok` and
`0.19.0-grok` already label other stacked branches, so this uses a distinct
`-voice.1` suffix instead of bumping into those numbers).

Concept inspiration only: GrokPuck-style “talk on the laptop, see a short
reply on a small screen.” Art on the device is the existing Carty G **v2
Grok Bot ball loader** already in this repo — original project art, not a
copy of any third-party frames.

This mode is **USB serial only** (Chromatic plugged into the laptop). It does
**not** claim Wi-Fi or wireless Grok for the voice path. The xAI call runs on
the laptop; the Chromatic only shows listening/thinking/captions.

## Protocol (USB CDC, 115200 8N1)

Host → device:

| Line | Effect |
| --- | --- |
| `voice` | Enter voice screen; device prints `VOICE_BOT <name>` then `VOICE_READY` |
| `voice listening` | Ball-loader “listening” state |
| `voice thinking` | Ball-loader “thinking” state |
| `VOICE_BEGIN` … lines … `VOICE_END` | Framed reply; device chunks into captions |
| `voice next` / `voice prev` | Cycle caption phrases |
| `voice bot` | Re-print `VOICE_BOT <name>` |
| `voice close` | Close overlay |

Device → host:

```
VOICE_BOT Carty G
VOICE_READY
```

D-pad **Left/Right** on the voice screen cycles the PR #11 bot list and
prints `VOICE_BOT <name>` again. **A / Down** advances captions; **Up** goes
back; **B / Start** closes.

Parsing is line-based with a bounded buffer (`GROK_VOICE_LINE_MAX` /
`GROK_VOICE_BODY_MAX`) and tolerates partial USB reads on the host side.

## Windows laptop install (Joe)

1. Python **3.10+** (3.11 or 3.12 recommended). From a fresh PowerShell:

```bat
py -3 --version
```

2. In the repo checkout:

```bat
cd chromatic-grok
py -3 -m pip install -r tools\requirements-voice.txt
```

First run of `faster-whisper` downloads the chosen model weights (default
`base.en`) into the local cache.

**Why faster-whisper:** speech-to-text stays on the laptop (no cloud STT key),
works without network for the listen step, and keeps the mic audio off the
wire. The only cloud call is the existing xAI Responses API, same as
`companion/proxy.py`.

3. Windows mic permission: Settings → Privacy → Microphone → allow desktop
   apps / allow the terminal you use.

4. Set the key **in the same window** (bring-your-own; never paste it into
   chat or commit it). The tool reads `XAI_API_KEY` from the environment
   only — it will not prompt for or echo the key:

```bat
set XAI_API_KEY=your-key-here
```

5. Plug in the Chromatic (USB). Close Miniterm / MR Updater / other serial
   apps. Then:

```bat
py -3 tools\voice_ptt.py --port COM4 --hotkey space
```

Hold **space** (or `--hotkey f9`) to talk; release to transcribe and send.
Ctrl+C stops the loop. Use `--once` for a single cycle.

Optional: `--whisper-model tiny.en` for a smaller/faster local model.

## Flash this firmware (USB)

**UNTESTED ON HARDWARE.** From an ESP-IDF 5.3 command prompt, with the
release files in the current folder, Chromatic on **COM4** at **115200**.
Close any serial monitor first. Do **not** erase flash; do **not** rewrite
the bootloader at `0x1000` unless you intend a full recovery.

Matching prior practice (`docs/REINSTALL.md`):

```bat
esptool.py --chip esp32 -p COM4 -b 115200 write_flash 0x8000 partition-table.bin 0x10000 chromatic_grok.bin 0x3F0000 ota_data_initial.bin
```

- Partition table at `0x8000`
- App at `0x10000`
- `ota_data_initial.bin` at `0x3F0000` so ota_0 boots after a USB write
- Bootloader offset `0x1000` is **not** included here (leave the existing
  bootloader alone for a normal app reflash)

After flash, open the serial console once if you need Wi-Fi for other Grok
modes; the laptop-voice path itself only needs USB serial + the laptop key.

## Host checks

```sh
sh tests/run_host.sh
```

Includes `tests/test_grok_voice.c` (framing + caption chunking).

## Risks

- Untested on hardware.
- `pynput` / mic permissions vary by Windows build.
- faster-whisper first download needs network; later runs can be offline for STT.
- Caption chunking is best-effort ASCII fold; long replies need A to page phrases.
