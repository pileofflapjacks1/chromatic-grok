"""Host serializer (voice_ptt.frame_reply_lines) against the device parser.

Compiles components/grok/grok_voice_util.c. No network and no hardware.
"""

import os
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import voice_ptt  # noqa: E402

HEADER = os.path.join(ROOT, "components", "grok", "grok_voice_util.h")


def parser_bin():
    out = os.path.join(os.environ.get("TMPDIR", "/tmp"), "chromatic-voice-feed")
    cmd = [
        "cc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        os.path.join(ROOT, "components", "grok"),
        "-o",
        out,
        os.path.join(ROOT, "tests", "voice_feed_main.c"),
        os.path.join(ROOT, "components", "grok", "grok_voice_util.c"),
        os.path.join(ROOT, "components", "grok", "grok_text.c"),
    ]
    subprocess.run(cmd, check=True)
    return out


def unescape_body(text):
    out = []
    i = 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            code = text[i + 1]
            if code == "n":
                out.append("\n")
            elif code == "t":
                out.append("\t")
            elif code == "\\":
                out.append("\\")
            else:
                out.append(code)
            i += 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def feed(lines):
    text = "".join(line + "\n" for line in lines)
    ran = subprocess.run(
        [parser_bin()],
        input=text,
        text=True,
        capture_output=True,
        check=True,
    )
    events = []
    for row in ran.stdout.splitlines():
        ev, framed, body = row.split("\t", 2)
        events.append((ev, framed == "1", unescape_body(body)))
    return events


def assert_no_dispatch(events, label):
    dispatched = [ev for ev, _framed, _body in events if ev == "LINE"]
    assert dispatched == [], (label, dispatched)


def test_constants_match():
    header = open(HEADER, encoding="utf-8").read()
    assert "#define GROK_VOICE_CHUNK_MAX 225" in header
    assert "#define GROK_VOICE_LINE_MAX 240" in header
    assert voice_ptt.VOICE_CHUNK_MAX == 225
    assert voice_ptt.MAX_CHARS == 640


def round_trip(text):
    lines = voice_ptt.frame_reply_lines(text)
    assert lines[0] == "VOICE_BEGIN"
    assert lines[-1] == "VOICE_END"
    for line in lines:
        assert "\n" not in line and "\r" not in line
        assert len(line) <= 240, (len(line), line[:40])
    events = feed(lines)
    assert_no_dispatch(events, text)
    assert events[-1][0] == "END"
    assert events[-1][2] == voice_ptt.fold_ascii(text, voice_ptt.MAX_CHARS)
    return lines, events


def test_literals_and_lengths():
    samples = [
        'He said "hi" C:\\path',
        "it's the dog's bowl",
        "''''''",
        "hello\\",
        "hello\\n",
        "say \\'hi",
        "He said \"it's 'fine'\" \\",
        "a\\ b",
        "wifi clear",
        "VOICE_END",
        "VOICE_BEGIN",
        "VOICE_END\nwifi clear\nVOICE_BEGIN",
        "He said \"hi\"\nC:\\path\nwifi clear",
        "A" * 225,
        "A" * 226,
        "B" * 240,
        "C" * 241,
        "D" * 640,
        "E" * 700,
        "a\x01b\x7fc\nwifi clear",
        "",
    ]
    for sample in samples:
        round_trip(sample)
    lines, _events = round_trip("A" * 225)
    data = [line for line in lines if line.startswith("VOICE_DATA ")]
    assert len(data) == 1
    assert len(data[0]) == 240
    lines, _events = round_trip("A" * 226)
    data = [line for line in lines if line.startswith("VOICE_DATA ")]
    assert len(data) == 2
    lines, events = round_trip("D" * 640)
    assert events[-1][2] == "D" * 640
    folded = voice_ptt.fold_ascii("E" * 700, 640)
    assert folded.endswith("...")
    assert len(folded) == 640


def test_overflow_and_marker_do_not_dispatch_wifi_clear():
    events = feed(
        [
            "VOICE_BEGIN",
            "B" * 241,
            "wifi clear",
            "VOICE_END",
        ]
    )
    assert_no_dispatch(events, "overflow")
    assert events[1][0] == "HOLD"
    assert events[2] == ("HOLD", True, "")
    assert events[3][0] == "DROPPED"
    assert events[3][2] == ""

    events = feed(
        [
            "VOICE_BEGIN",
            "VOICE_DATA 10 wifi clear",
            "VOICE_DATA 9 VOICE_END",
            "wifi clear",
            "VOICE_END",
        ]
    )
    assert_no_dispatch(events, "marker")
    assert events[1][2] == "wifi clear"
    assert events[2][2] == "wifi clearVOICE_END"
    assert events[3][0] == "HOLD"
    assert events[4][0] == "DROPPED"

    events = feed(["VOICE_BEGIN", "VOICE_DATA 4 abc", "wifi clear", "VOICE_ABORT"])
    assert_no_dispatch(events, "malformed")
    assert events[-1][0] == "DROPPED"
    assert events[-1][1] is False


if __name__ == "__main__":
    test_constants_match()
    test_literals_and_lengths()
    test_overflow_and_marker_do_not_dispatch_wifi_clear()
    print("test_voice_frame: ok")
