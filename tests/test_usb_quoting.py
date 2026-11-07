"""Round-trip quote_arg through the pinned ESP-IDF v5.3 USB splitter.

No network. BLE split_args is checked only to show it is a different parser.
"""

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "companion"))

import chromatic_grok  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")


def parser_bin():
    built = subprocess.run(
        ["sh", "tests/build_usb_split_argv.sh"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return built.stdout.strip().splitlines()[-1]


def split(which, line):
    ran = subprocess.run(
        [parser_bin(), which, line],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(ran.stdout)


def show_text(argv):
    assert argv[:2] == ["grok", "show"], argv
    assert len(argv) == 3, argv
    return argv[2]


def test_quote_arg_usb_round_trip():
    cases = [
        ("C:\\path", "C:\\path"),
        ("hello\\", "hello\\"),
        ("hello\\n", "hello\\n"),
        ("\\n", "\\n"),
        ('Say "hi"', 'Say "hi"'),
        ("it's the dog's bowl", "it's the dog's bowl"),
        ("''''''", "''''''"),
        ("He said \"it's 'fine'\"", "He said \"it's 'fine'\""),
        ("a\\ b", "a\\ b"),
        ("", ""),
        ("hello\nwifi clear", "hello wifi clear"),
        ("a\tb", "a b"),
        ("a\x01b\x7fc", "abc"),
        ("\\", "\\"),
        ('""""', '""""'),
        ("say \\'hi", "say \\'hi"),
    ]
    for raw, expected in cases:
        quoted = chromatic_grok.quote_arg(raw)
        assert "\n" not in quoted and "\r" not in quoted
        argv = split("usb", "grok show " + quoted)
        got = show_text(argv)
        assert got == expected, (raw, quoted, got)


def test_usb_quote_is_not_ble_round_trip():
    quoted = chromatic_grok.quote_arg('Say "hi"')
    line = "grok show " + quoted
    assert show_text(split("usb", line)) == 'Say "hi"'
    ble = split("ble", line)
    ble_text = ble[2] if len(ble) == 3 else "\n".join(ble)
    assert ble_text != 'Say "hi"'


if __name__ == "__main__":
    test_quote_arg_usb_round_trip()
    test_usb_quote_is_not_ble_round_trip()
    print("test_usb_quoting: ok")
