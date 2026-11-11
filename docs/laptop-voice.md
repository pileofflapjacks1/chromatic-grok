# Laptop voice → Chromatic display

**UNTESTED ON HARDWARE.**

Stacked on the bot-menu branch (`0.17.1-grok`). App version for this add-on:
`0.17.1-voice.5` (keeps the 0.17.1 bot-menu baseline; `0.18.0-grok` and
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

## Captions and screen

**UNTESTED ON HARDWARE.** The look below was checked only in the host preview
(`python3 tools/voice_preview.py out.png`), which draws from the same layout
numbers and the real unscii_8 glyph tables. It is not LVGL and not a photo.

The voice screen (all three states) is a warm amber header bar. The bot name
is on the left and the state is on the right: `Listening`, `Thinking` or
`Reply`. The name is cut with a trailing `.` if it would touch the state word.
The background is deep navy. Listening and Thinking show the Carty G ball in
cream with the three dots in cyan (listening) or amber (thinking). A reply is
cream text in a rounded, cyan-bordered panel. The footer bar shows
`A next B exit`, the page as `2/4`, and up/down triangles that go dim when
there is no page in that direction. A long page count drops `B exit` to keep
the footer from overlapping.

unscii_8 is an 8×8 font. The panel border and padding take room from the 20
columns by 13 rows of the plain reply page, so one voice caption page is **18
columns by 11 rows** (144 px by 99 px, 9 px rows). The numbers are
`GROK_VOICE_*` in `components/grok/grok_voice_util.h` and compile-time and host
checks make sure the text fits inside the border. The plain reply page and the
picker are still 20×13. A/Down moves to the next page of wrapped lines and
stops on the last page. Up moves back and stops on the first. B and Start
still close. Left and Right still change the bot.

Spaces and tabs collapse to one space between words. A newline starts a
new row, including a blank row. A word longer than 18 characters is split
every 18 characters. The stored reply is still at most 640 characters (a
640-character single word is 36 rows, 4 pages).

The device is sent only the reply, not the words you spoke, so there is no
"You:" line. Showing one needs a new `VOICE_*` line from `voice_ptt.py`.

## Windows laptop install (Joe)

A GitHub branch ZIP does not unpack into a folder named exactly
`chromatic-grok`. Code → Download ZIP for
`awesome-coder/voice-safe-framing` extracts a nested directory whose name
looks like `chromatic-grok-awesome-coder-voice-safe-framing`. Open that
directory and confirm `tools\voice_ptt.py` is there before the commands
below. If `tools` is not in the current directory, `pip` and `py` will not
see these files.

1. Python **3.10+**. Joe used Python 3.13 for the COM-port open that printed
   `VOICE_BOT` and `VOICE_READY`. `faster-whisper` 1.1.1 declares
   `Requires-Python >=3.9` and its classifiers list 3.9 through 3.11. This
   workspace checked the requirements file with Python 3.12.3 and did not
   install the full speech stack on 3.13.

```bat
py -3 --version
```

2. From the directory that contains `tools`:

```bat
py -3 -m pip install -r tools\requirements-voice.txt
```

`tools\requirements-voice.txt` pins `requests==2.34.2` because
`faster-whisper` 1.1.1 pulls in `huggingface-hub`, and that import failed
on Python 3.13 with `ModuleNotFoundError: requests`. `2.34.2` is the
current release on PyPI, requires Python `>=3.10`, and the project
vulnerability list was empty when the pin was added.

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
   apps. `tools\voice_ptt.py` opens the COM port with pyserial's `dtr` and
   `rts` set False on the closed port, then calls `open()`. `rtscts`,
   `dsrdtr`, and `xonxoff` stay off. pyserial 3.5's `Serial(port)` opens
   immediately with DTR and RTS left at their default True; clearing them
   after that is too late for `open()`.

   Joe, via Carty, reported one Windows Python 3.13 open of COM4 with that
   before-open ordering. The device printed `VOICE_BOT Carty G` and
   `VOICE_READY` and did not freeze. The microphone, speech-to-text, and
   caption path was not part of that report. pyserial's own `open()`
   documentation says some operating systems or drivers can still assert
   RTS or DTR when the port opens, even if the requested level is False.
   This tree does not claim that driver glitch is gone.

   Optional, still no microphone and no network:

```bat
py -3 tools\voice_probe.py --port COM4
```

   Push-to-talk:

```bat
py -3 tools\voice_ptt.py --list-mics
py -3 tools\voice_ptt.py --port COM4 --hotkey space
py -3 tools\voice_ptt.py --port COM4 --mic 1 --hotkey space
```

`--list-mics` prints inputs only. With no `--mic`, the Windows default is
used when its name does not contain `Chromatic`. Otherwise the first other
input is used. If every input is a Chromatic device, the tool stops and
lists them instead of recording silence. `--mic 1` or `--mic Realtek` may
select a Chromatic device on purpose. The chosen `mic: [index] name` line
is printed before recording.

Hold **space** (or `--hotkey f9`) to talk; release to transcribe and send.
Ctrl+C stops the loop. Use `--once` for a single cycle.

Optional: `--whisper-model tiny.en` for a smaller/faster local model.

## Flash this firmware (USB)

**UNTESTED ON HARDWARE.** The screen look and the 18×11 caption page need `0.17.1-voice.5`.
Leave the existing `0.17.1-voice.3` draft release assets in place and
upload this image as a new draft. From an ESP-IDF 5.3 command prompt, with the
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
