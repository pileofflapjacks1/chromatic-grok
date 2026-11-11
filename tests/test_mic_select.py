"""Mocked microphone selection. No sounddevice and no hardware."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import mic_select  # noqa: E402

DEVICES = [
    {"name": "Microphone (Chromatic - Player 01)", "max_input_channels": 1},
    {"name": "Microphone (Realtek)", "max_input_channels": 2},
    {"name": "Speakers", "max_input_channels": 0},
    {"name": "Headset Mic", "max_input_channels": 1},
]


def test_default_skips_chromatic_even_when_it_is_default():
    index, name = mic_select.select_input(DEVICES, 0, None)
    assert index == 1
    assert name == "Microphone (Realtek)"


def test_default_keeps_a_real_default():
    index, name = mic_select.select_input(DEVICES, 3, None)
    assert (index, name) == (3, "Headset Mic")


def test_only_chromatic_fails():
    only = [{"name": "Microphone (chromatic)", "max_input_channels": 1}]
    try:
        mic_select.select_input(only, 0, None)
    except ValueError as exc:
        text = str(exc)
        assert "Chromatic" in text
        assert "[0]" in text
    else:
        raise AssertionError("Chromatic-only default was accepted")


def test_explicit_index_may_select_chromatic():
    index, name = mic_select.select_input(DEVICES, 1, "0")
    assert index == 0
    assert "Chromatic" in name


def test_missing_index_and_ambiguous_name():
    try:
        mic_select.select_input(DEVICES, 1, "9")
    except ValueError as exc:
        assert "index 9" in str(exc)
    else:
        raise AssertionError("missing index accepted")
    try:
        mic_select.select_input(DEVICES, 1, "mic")
    except ValueError as exc:
        assert "more than one" in str(exc)
        assert "Realtek" in str(exc)
        assert "Headset" in str(exc)
    else:
        raise AssertionError("ambiguous name accepted")


def test_list_is_inputs_only():
    text = mic_select.format_inputs(DEVICES)
    assert "Speakers" not in text
    assert "[1] Microphone (Realtek)" in text
    assert "[3] Headset Mic" in text


def test_unique_substring_is_case_insensitive():
    index, name = mic_select.select_input(DEVICES, 0, "headset")
    assert (index, name) == (3, "Headset Mic")


if __name__ == "__main__":
    test_default_skips_chromatic_even_when_it_is_default()
    test_default_keeps_a_real_default()
    test_only_chromatic_fails()
    test_explicit_index_may_select_chromatic()
    test_missing_index_and_ambiguous_name()
    test_list_is_inputs_only()
    test_unique_substring_is_case_insensitive()
    print("test_mic_select: ok")
