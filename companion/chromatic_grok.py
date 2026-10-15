#!/usr/bin/env python3
# Chromatic Grok companion. Part of a GPL-3.0-or-later modified work based on
# the ModRetro Chromatic MCU firmware. See ../LICENSE and ../NOTICE.
"""Serial companion for the Chromatic Grok firmware.

Talks to the mcu> console at 115200. The device can POST the prompt to the
LAN proxy, or this script can call xAI itself and push the reply with
`grok show` (--via-host). BLE is documented in docs/PROTOCOL.md and is not
required.
"""

import argparse
import glob
import os
import sys
import time

try:
    import serial
except ImportError:
    serial = None

import proxy


def default_port():
    chosen = os.environ.get("CHROMATIC_PORT", "").strip()
    if chosen:
        return chosen
    matches = sorted(glob.glob("/dev/cu.usbmodem*"))
    if matches:
        return matches[0]
    if os.path.exists("/dev/ttyACM0"):
        return "/dev/ttyACM0"
    return ""


def quote_arg(text):
    cleaned = text.replace('"', "'").replace("\r", " ").replace("\n", " ")
    if cleaned == "" or any(ch in cleaned for ch in " \t"):
        return '"' + cleaned + '"'
    return cleaned


def open_port(path):
    if serial is None:
        sys.stderr.write("pyserial is not installed. Run: python3 -m pip install -r companion/requirements.txt\n")
        return None
    port = serial.Serial()
    port.port = path
    port.baudrate = 115200
    port.timeout = 0.2
    port.dtr = False
    port.rts = False
    port.open()
    return port


def read_until(port, marker, timeout):
    deadline = time.time() + timeout
    buf = bytearray()
    needle = marker.encode("utf-8")
    while time.time() < deadline:
        chunk = port.read(256)
        if chunk:
            buf.extend(chunk)
            if needle in buf:
                break
        elif needle in buf:
            break
    return buf.decode("utf-8", "replace")


def wait_for_prompt(port, timeout):
    text = read_until(port, "mcu> ", timeout)
    if "mcu> " in text:
        return text
    port.write(b"\n")
    port.flush()
    return text + read_until(port, "mcu> ", timeout)


def framed_reply(text):
    begin = text.rfind("GROK_BEGIN")
    end = text.rfind("GROK_END")
    if begin < 0 or end < begin:
        return None
    body = text[begin + len("GROK_BEGIN"):end]
    return body.strip("\r\n")


def run_line(port, line, timeout):
    port.write((line + "\n").encode("utf-8"))
    port.flush()
    text = read_until(port, "mcu> ", timeout)
    reply = framed_reply(text)
    if reply is not None:
        print(reply)
    else:
        cleaned = text.replace("mcu> ", "").strip()
        if cleaned:
            print(cleaned)
    return text


def command_ask(port, prompt, via_host):
    if via_host:
        reply, err = proxy.ask_xai(prompt)
        if err:
            sys.stderr.write(err + "\n")
            return 1
        run_line(port, "grok show " + quote_arg(reply), 5)
        return 0
    run_line(port, "grok ask " + quote_arg(prompt), 45)
    return 0


def repl(port):
    sys.stderr.write("Connected. Type console commands. Ctrl-C exits.\n")

    def pump():
        while True:
            data = port.read(256)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()

    import threading
    thread = threading.Thread(target=pump, daemon=True)
    thread.start()
    try:
        while True:
            line = sys.stdin.readline()
            if line == "":
                break
            port.write(line.encode("utf-8"))
            port.flush()
    except KeyboardInterrupt:
        sys.stderr.write("\n")
    return 0


def build_parser():
    parser = argparse.ArgumentParser(description="Chromatic Grok serial companion")
    parser.add_argument("--port", default=default_port(), help="Serial device. Defaults to CHROMATIC_PORT, then a usbmodem or ttyACM0 port.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    wifi = sub.add_parser("wifi-set", help="Save SSID and password on the device")
    wifi.add_argument("ssid")
    wifi.add_argument("password", nargs="?", default="")

    sub.add_parser("wifi-status", help="Print the Wi-Fi status line")
    sub.add_parser("status", help="Print the Grok link status")

    url = sub.add_parser("url-set", help="Save the proxy URL (http:// only)")
    url.add_argument("url")

    ask = sub.add_parser("ask", help="Send a prompt")
    ask.add_argument("prompt")
    ask.add_argument("--via-host", action="store_true", help="Call xAI from this computer, then grok show the reply")

    show = sub.add_parser("show", help="Display text already on this computer")
    show.add_argument("text")

    sub.add_parser("repl", help="Pass keys straight to the mcu> console")
    return parser


def main(argv):
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.port:
        sys.stderr.write("No serial port. Pass --port /dev/cu.usbmodem... or set CHROMATIC_PORT.\n")
        return 1
    port = open_port(args.port)
    if port is None:
        return 1
    try:
        banner = wait_for_prompt(port, 8)
        if "mcu> " not in banner:
            sys.stderr.write("Did not see the mcu> prompt on %s. The console may still be booting.\n" % args.port)
        if args.cmd == "repl":
            return repl(port)
        if args.cmd == "wifi-set":
            line = "wifi set " + quote_arg(args.ssid)
            if args.password:
                line += " " + quote_arg(args.password)
            run_line(port, line, 8)
            return 0
        if args.cmd == "wifi-status":
            run_line(port, "wifi status", 5)
            return 0
        if args.cmd == "status":
            run_line(port, "grok status", 5)
            return 0
        if args.cmd == "url-set":
            run_line(port, "grok url set " + quote_arg(args.url), 5)
            return 0
        if args.cmd == "ask":
            return command_ask(port, args.prompt, args.via_host)
        if args.cmd == "show":
            run_line(port, "grok show " + quote_arg(args.text), 5)
            return 0
    finally:
        port.close()
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
