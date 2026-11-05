#!/usr/bin/env python3
# Chromatic laptop-voice PTT. Part of a GPL-3.0-or-later modified work.
# See ../LICENSE and ../NOTICE.
"""Hold a key to talk; local STT; Grok reply framed to the Chromatic over USB serial.

UNTESTED ON HARDWARE.

Security:
- Reads XAI_API_KEY from the environment only (same bring-your-own flow as
  companion/proxy.py). Never prompts for the key, never logs it, never writes
  it to disk.
- Opens the Chromatic COM port only when the device is plugged in.
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import re
import sys
import threading
import time
import urllib.error
import urllib.request

# Optional heavy deps are imported after argparse so --help works without them.

SYSTEM = (
    "You are answering on a 160x144 handheld with an 8px font. "
    "Reply in short plain ASCII. No markdown, no lists, no emoji. "
    "A few short sentences at most."
)
DEFAULT_MODEL = "grok-4.7"
XAI_URL = "https://api.x.ai/v1/responses"
MAX_CHARS = 640
_KEY_PATTERN = re.compile(r"xai-[A-Za-z0-9_\-]+")


def redact(text: str) -> str:
    if not text:
        return text
    key = os.environ.get("XAI_API_KEY", "").strip()
    if key:
        text = text.replace(key, "[redacted]")
    return _KEY_PATTERN.sub("[redacted]", text)


def fold_ascii(text: str, max_chars: int = MAX_CHARS) -> str:
    if text is None:
        text = ""
    out = []
    for ch in text:
        code = ord(ch)
        if ch == "\n" or 32 <= code <= 126:
            out.append(ch)
        elif ch == "\t":
            out.append(" ")
        elif ch == "\r":
            if not out or out[-1] != "\n":
                out.append("\n")
        else:
            out.append("?")
    folded = "".join(out).strip()
    if len(folded) > max_chars:
        folded = folded[: max_chars - 3].rstrip() + "..."
    return folded


def extract_reply(payload):
    if not isinstance(payload, dict):
        return ""
    direct = payload.get("output_text")
    if isinstance(direct, str) and direct.strip():
        return direct.strip()
    output = payload.get("output")
    if isinstance(output, list):
        parts = []
        for item in output:
            if not isinstance(item, dict):
                continue
            for content in item.get("content", []) or []:
                if isinstance(content, dict) and content.get("type") in (
                    "output_text",
                    "text",
                ):
                    text = content.get("text")
                    if isinstance(text, str):
                        parts.append(text)
        if parts:
            return "\n".join(parts).strip()
    err = payload.get("error")
    if isinstance(err, dict):
        msg = err.get("message") or err.get("code")
        if msg:
            return str(msg).strip()
    return ""


def ask_xai(prompt: str, timeout: int = 30):
    key = os.environ.get("XAI_API_KEY", "").strip()
    if not key:
        return None, "XAI_API_KEY is not set"
    model = os.environ.get("XAI_MODEL", DEFAULT_MODEL).strip() or DEFAULT_MODEL
    body = {
        "model": model,
        "input": [
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": prompt},
        ],
        "max_output_tokens": 180,
    }
    req = urllib.request.Request(
        XAI_URL,
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Authorization": "Bearer " + key,
            "Content-Type": "application/json",
            "User-Agent": "chromatic-grok-voice-ptt",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", "replace")
        payload = json.loads(raw)
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", "replace")
        try:
            payload = json.loads(raw)
        except json.JSONDecodeError:
            return None, "upstream http %s" % exc.code
        return None, redact(extract_reply(payload) or ("upstream http %s" % exc.code))
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
        return None, "upstream unreachable"
    reply = fold_ascii(extract_reply(payload))
    if not reply:
        return None, "empty reply"
    return reply, None


class ChromaticSerial:
    def __init__(self, port: str, baud: int = 115200):
        import serial

        self._ser = serial.Serial(port, baudrate=baud, timeout=0.2)
        self._lock = threading.Lock()
        self.bot_name = None
        self._rx_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._rx_thread.start()

    def close(self):
        try:
            self._ser.close()
        except Exception:
            pass

    def write_line(self, line: str):
        data = (line.rstrip("\r\n") + "\n").encode("ascii", "replace")
        with self._lock:
            self._ser.write(data)
            self._ser.flush()

    def send_framed_reply(self, text: str):
        self.write_line("VOICE_BEGIN")
        for raw in text.splitlines() or [text]:
            line = fold_ascii(raw, MAX_CHARS)
            if line:
                self.write_line(line)
        self.write_line("VOICE_END")

    def _read_loop(self):
        buf = b""
        while self._ser.is_open:
            try:
                chunk = self._ser.read(256)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").strip("\r")
                if text.startswith("VOICE_BOT "):
                    self.bot_name = text[len("VOICE_BOT ") :].strip()
                    sys.stderr.write("device bot: %s\n" % self.bot_name)
                elif text:
                    # Device also echoes console prompts; keep stderr tidy.
                    if text.startswith("mcu>") or text.startswith("voice:"):
                        sys.stderr.write("%s\n" % text)


def record_until(stop_event: threading.Event, sample_rate: int = 16000):
    import numpy as np
    import sounddevice as sd

    chunks = []

    def callback(indata, frames, time_info, status):  # noqa: ARG001
        if status:
            sys.stderr.write("mic status: %s\n" % status)
        chunks.append(indata.copy())

    with sd.InputStream(
        samplerate=sample_rate, channels=1, dtype="float32", callback=callback
    ):
        while not stop_event.is_set():
            time.sleep(0.05)
    if not chunks:
        return np.zeros(0, dtype="float32"), sample_rate
    audio = np.concatenate(chunks, axis=0).reshape(-1)
    return audio, sample_rate


def transcribe(audio, sample_rate: int, model_size: str) -> str:
    # faster-whisper: local STT so the laptop needs no cloud speech key,
    # works offline for the listen step, and keeps latency on-device.
    from faster_whisper import WhisperModel

    if audio.size == 0:
        return ""
    model = WhisperModel(model_size, device="cpu", compute_type="int8")
    segments, _info = model.transcribe(audio, language="en", vad_filter=True)
    parts = [seg.text.strip() for seg in segments if seg.text and seg.text.strip()]
    return fold_ascii(" ".join(parts), 2000)


def wait_ptt(hotkey: str, on_start=None):
    from pynput import keyboard

    pressed = threading.Event()
    released = threading.Event()

    # Map common names.
    key_lookup = {
        "space": keyboard.Key.space,
        "f9": keyboard.Key.f9,
        "f8": keyboard.Key.f8,
        "f10": keyboard.Key.f10,
    }
    target = key_lookup.get(hotkey.lower())
    if target is None and len(hotkey) == 1:
        target = keyboard.KeyCode.from_char(hotkey.lower())
    if target is None:
        raise SystemExit("unsupported hotkey %r (try space or f9)" % hotkey)

    def on_press(key):
        if key == target:
            pressed.set()

    def on_release(key):
        if key == target:
            released.set()
            return False
        return True

    sys.stderr.write("Hold %s to talk (Ctrl+C to quit)...\n" % hotkey)
    with keyboard.Listener(on_press=on_press) as listener:
        while not pressed.is_set():
            time.sleep(0.05)
            if not listener.running:
                raise SystemExit(0)
    if on_start is not None:
        on_start()
    released.clear()
    stop = threading.Event()
    rec_thread_result = {}

    def _rec():
        audio, sr = record_until(stop)
        rec_thread_result["audio"] = audio
        rec_thread_result["sr"] = sr

    t = threading.Thread(target=_rec, daemon=True)
    t.start()
    with keyboard.Listener(on_release=on_release) as listener:
        listener.join()
    stop.set()
    t.join()
    return rec_thread_result.get("audio"), rec_thread_result.get("sr", 16000)


def one_shot(port: str, hotkey: str, model_size: str, baud: int):
    link = ChromaticSerial(port, baud=baud)
    try:
        link.write_line("voice")
        time.sleep(0.3)
        audio, sr = wait_ptt(hotkey, on_start=lambda: link.write_line("voice listening"))
        sys.stderr.write("transcribing locally (faster-whisper %s)...\n" % model_size)
        link.write_line("voice thinking")
        text = transcribe(audio, sr, model_size)
        if not text:
            sys.stderr.write("no speech heard\n")
            return 1
        bot = link.bot_name or "Grok"
        prompt = "Bot: %s\nUser: %s" % (bot, text)
        sys.stderr.write("heard: %s\n" % text)
        reply, err = ask_xai(prompt)
        if err:
            sys.stderr.write("grok error: %s\n" % err)
            link.write_line("VOICE_BEGIN")
            link.write_line(fold_ascii(err))
            link.write_line("VOICE_END")
            return 2
        sys.stderr.write("reply: %s\n" % reply)
        link.send_framed_reply(reply)
        return 0
    finally:
        link.close()


def loop_mode(port: str, hotkey: str, model_size: str, baud: int):
    link = ChromaticSerial(port, baud=baud)
    try:
        link.write_line("voice")
        time.sleep(0.3)
        while True:
            audio, sr = wait_ptt(hotkey, on_start=lambda: link.write_line("voice listening"))
            sys.stderr.write("transcribing locally (faster-whisper %s)...\n" % model_size)
            link.write_line("voice thinking")
            text = transcribe(audio, sr, model_size)
            if not text:
                sys.stderr.write("no speech heard\n")
                continue
            bot = link.bot_name or "Grok"
            prompt = "Bot: %s\nUser: %s" % (bot, text)
            sys.stderr.write("heard: %s\n" % text)
            reply, err = ask_xai(prompt)
            if err:
                sys.stderr.write("grok error: %s\n" % err)
                link.send_framed_reply(fold_ascii(err))
                continue
            sys.stderr.write("reply: %s\n" % reply)
            link.send_framed_reply(reply)
    except KeyboardInterrupt:
        sys.stderr.write("\nstopped\n")
        return 0
    finally:
        link.close()


def main(argv):
    parser = argparse.ArgumentParser(
        description="Laptop push-to-talk -> Chromatic voice captions (USB serial)"
    )
    parser.add_argument(
        "--port",
        required=True,
        help="Chromatic COM port, e.g. COM4 (device must be plugged in)",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--hotkey",
        default="space",
        help="Hold-to-talk key (space, f9, ...). Default: space",
    )
    parser.add_argument(
        "--whisper-model",
        default="base.en",
        help="faster-whisper model size (tiny.en|base.en|small.en). Default base.en",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="One PTT cycle then exit (default: loop until Ctrl+C)",
    )
    args = parser.parse_args(argv)

    if not os.environ.get("XAI_API_KEY", "").strip():
        sys.stderr.write(
            "XAI_API_KEY is not set. Set it in this window before running "
            "(same bring-your-own flow as companion/proxy.py). "
            "This tool never prompts for the key.\n"
        )
        return 2

    # Import heavy deps early so failures are clear on Joe's Windows laptop.
    try:
        import numpy  # noqa: F401
        import serial  # noqa: F401
        import sounddevice  # noqa: F401
        from pynput import keyboard  # noqa: F401
    except ImportError as exc:
        sys.stderr.write(
            "Missing dependency: %s\nInstall with:\n  pip install -r tools/requirements-voice.txt\n"
            % exc
        )
        return 2

    if args.once:
        return one_shot(args.port, args.hotkey, args.whisper_model, args.baud)
    return loop_mode(args.port, args.hotkey, args.whisper_model, args.baud)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
