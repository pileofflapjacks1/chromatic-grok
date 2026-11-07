# Laptop voice → Chromatic display

**UNTESTED ON HARDWARE.**

Stacked on the bot-menu branch (`0.17.1-grok`). App version for this add-on:
`0.17.1-voice.3` (keeps the 0.17.1 bot-menu baseline; `0.18.0-grok` and
`0.19.0-grok` already label other stacked branches, so this stays on the
`0.17.1-voice` line instead of bumping into those numbers).

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
| `VOICE_BEGIN` | Start a reply frame and clear the body | 
| `VOICE_DATA <len> <payload>` | Append exactly `<len>` literal payload bytes | 
| `VOICE_NL` | Append one newline | 
| `VOICE_END` | Finish the frame. A clean body is shown. A failed frame is dropped | 
| `VOICE_ABORT` | Drop the frame and return to the console |
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

Payload bytes are literal. The host does not turn `"` into `'` and does not
treat `\` as an escape. A newline in the reply is a `VOICE_NL` line because
the console ends a command on CR/LF. `VOICE_CHUNK_MAX` is 225 so a
`VOICE_DATA` line is at most `GROK_VOICE_LINE_MAX` (240). The body cap and
the host cap are both 640 characters.

`0.17.1-voice.1` sent raw body lines between `VOICE_BEGIN` and `VOICE_END`.
This version does not. An old host against this firmware gets `voice:
discarded` instead of a caption, and those raw lines are not executed. No
field device is known to be running `0.17.1-voice.1` (that build was
untested on hardware).

Recovery:

- The frame stays open until a line that is exactly `VOICE_END` or exactly
  `VOICE_ABORT`. There is no line cap and no timer that returns to the
  console in the middle of a frame. Memory stays bounded: the body buffer is
  cleared when a chunk is bad, and it is not grown past 640 bytes.
- `VOICE_END` shows the body when every chunk was well formed. If any chunk
  was overlong, the wrong length, or not a `VOICE_DATA` / `VOICE_NL` line,
  the body is cleared and the device prints `voice: discarded`.
- `VOICE_ABORT` clears the body without showing a reply.
- `wifi clear`, `voice close`, and further reply lines after hundreds of bad
  lines are still inside the frame. They do not reach `esp_console_run`.
- A length field is a 1 to 3 digit count followed by one space. The payload
  must be exactly that many bytes. `VOICE_DATA 3 wifi clear`, `VOICE_DATA
  9999 ...`, a leading `+`, or a missing space fails the frame and stays
  inside it.
- Marker collision: `VOICE_DATA 9 VOICE_END` is caption text. A raw line that
  is exactly `VOICE_END` is the terminator (legacy raw framing uses that).
  `VOICE_END ` with a trailing space is not the terminator. Raw legacy body
  lines before that exact marker are not executed. The new host never emits
  a raw `VOICE_END` inside the body.
- `voice close` typed during an open frame is frame junk, not the close
  verb. Send `VOICE_ABORT` or `VOICE_END` to unlock the console. A truncated
  frame with no end marker keeps swallowing lines on purpose.

A line inside an open frame is never passed to `esp_console_run`. That
includes `wifi clear` after a 241-character line, after more than 700 bad
lines, and a payload whose text is `VOICE_END` or `wifi clear`.

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

Includes `tests/test_grok_voice.c` and `tests/test_voice_frame.py` (device
parser fed by `frame_reply_lines`, plus the `wifi clear` dispatch regression).

## Risks

- **UNTESTED ON HARDWARE.**
- Incompatible with the `0.17.1-voice.1` raw-line frame.
- `pynput` / mic permissions vary by Windows build.
- faster-whisper first download needs network; later runs can be offline for STT.
- Caption chunking is best-effort ASCII fold; long replies need A to page phrases.
