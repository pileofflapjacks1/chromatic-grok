"""Host checks for the proxy reply parser. No network."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "companion"))

from proxy import extract_reply, fold_ascii


def test_output_text():
    assert extract_reply({"output_text": "Hello"}) == "Hello"


def test_output_blocks():
    payload = {
        "output": [
            {"content": [{"type": "output_text", "text": "Hi"}, {"type": "output_text", "text": "there"}]}
        ]
    }
    assert extract_reply(payload) == "Hi\nthere"


def test_error_object():
    assert extract_reply({"error": {"message": "bad key"}}) == "bad key"


def test_empty():
    assert extract_reply({}) == ""
    assert extract_reply({"output_text": "  "}) == ""


def test_fold():
    assert fold_ascii("Cafe\u00e9\nline", 640) == "Cafe?\nline"
    assert fold_ascii("abcdef", 5) == "ab..."


if __name__ == "__main__":
    test_output_text()
    test_output_blocks()
    test_error_object()
    test_empty()
    test_fold()
    print("proxy extract ok")
