#!/usr/bin/env python3
"""Send one ``voice`` line and print whether the device answers.

No microphone, no speech model, and no network. Does not read XAI_API_KEY.

Joe, via Carty, saw VOICE_BOT and VOICE_READY from one manual open that set
DTR and RTS false before open(). This script uses that same helper. It has
not been run on the Chromatic from this workspace. A missing answer does not
by itself explain why.
"""

import argparse
import sys
import time

from console_port import open_console_port


def probe(port, baud, timeout):
    ser = open_console_port(port, baud=baud, timeout=0.2)
    try:
        ser.write(b"voice\n")
        ser.flush()
        deadline = time.time() + timeout
        buf = bytearray()
        while time.time() < deadline:
            chunk = ser.read(256)
            if chunk:
                buf.extend(chunk)
                if b"VOICE_READY" in buf:
                    break
            else:
                time.sleep(0.05)
        text = buf.decode("utf-8", "replace")
        sys.stdout.write(text)
        if "VOICE_READY" not in text and "VOICE_BOT " not in text:
            sys.stderr.write("no VOICE_BOT or VOICE_READY in %.1fs\n" % timeout)
            return 1
        return 0
    finally:
        try:
            ser.close()
        except Exception:
            pass


def main(argv):
    parser = argparse.ArgumentParser(description="One voice-ready probe over USB serial")
    parser.add_argument("--port", required=True, help="Chromatic COM port, for example COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args(argv)
    try:
        return probe(args.port, args.baud, args.timeout)
    except Exception as exc:
        sys.stderr.write("serial open failed: %s\n" % exc)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
