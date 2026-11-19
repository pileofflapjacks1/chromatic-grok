"""Host checks for the proxy reply parser. No network."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "companion"))

import proxy  # noqa: E402
from proxy import build_request, extract_reply, fold_ascii, search_tools, strip_citations  # noqa: E402

# Shape of an xAI Responses API body after x_search + web_search, per
# https://docs.x.ai/developers/tools/citations and tool-usage-details.
SEARCH_RESPONSE = {
    "id": "resp_test",
    "object": "response",
    "model": "grok-4.7",
    "status": "completed",
    "output": [
        {"type": "reasoning", "id": "rs_1", "summary": [{"type": "summary_text", "text": "Need to look up the account."}]},
        {"type": "custom_tool_call", "call_id": "xs_1", "name": "x_user_search",
         "input": "{\"query\": \"CardCaptain\"}", "status": "completed"},
        {"type": "web_search_call", "id": "ws_1", "status": "completed",
         "action": {"type": "search", "query": "CardCaptain X account"}},
        {"type": "message", "role": "assistant", "status": "completed", "content": [
            {"type": "output_text",
             "text": "@CardCaptain posts about trading card games [[1]](https://x.com/CardCaptain/status/1). "
                     "Recent posts cover Pokemon pulls.",
             "annotations": [{"type": "url_citation", "url": "https://x.com/CardCaptain/status/1",
                              "start_index": 45, "end_index": 82, "title": "1"}]},
        ]},
    ],
    "citations": ["https://x.com/CardCaptain/status/1", "https://example.com/cards"],
    "usage": {"input_tokens": 900, "output_tokens": 120,
              "server_side_tool_usage_details": {"web_search_calls": 1, "x_search_calls": 1,
                                                 "x_posts_fetched": 10, "x_users_fetched": 1}},
}


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


def test_search_response():
    text = extract_reply(SEARCH_RESPONSE)
    assert "Need to look up" not in text, text
    assert "CardCaptain X account" not in text, text
    assert "query" not in text, text
    assert "example.com" not in text, text
    clean = fold_ascii(strip_citations(text), 640)
    assert clean == "@CardCaptain posts about trading card games. Recent posts cover Pokemon pulls.", clean


def test_only_tool_items():
    payload = {"output": [SEARCH_RESPONSE["output"][1], SEARCH_RESPONSE["output"][2]]}
    assert extract_reply(payload) == ""


def test_strip_citations():
    assert strip_citations("Yes [[2]](https://a.b/c).") == "Yes."
    assert strip_citations("See [the post](https://x.com/a/1) now") == "See the post now"
    assert strip_citations("Go to https://x.com/a for more") == "Go to for more"
    assert strip_citations("Fact [1][2] here") == "Fact here"
    assert strip_citations("plain @name text") == "plain @name text"


def test_search_tools_env():
    assert [t["type"] for t in search_tools("on")] == ["x_search", "web_search"]
    assert [t["type"] for t in search_tools("x")] == ["x_search"]
    assert [t["type"] for t in search_tools("web")] == ["web_search"]
    assert search_tools("off") == []
    assert search_tools("0") == []
    assert [t["type"] for t in search_tools("bogus")] == ["x_search", "web_search"]


def test_build_request():
    for name in ("XAI_SEARCH", "XAI_SEARCH_MAX_TURNS", "XAI_TIMEOUT"):
        os.environ.pop(name, None)
    body, timeout = build_request("Who is @CardCaptain on X", model="grok-4.7")
    assert body["tools"] == [{"type": "x_search"}, {"type": "web_search"}]
    assert body["include"] == ["no_inline_citations"]
    assert body["max_turns"] == proxy.DEFAULT_MAX_TURNS
    assert body["max_output_tokens"] == proxy.SEARCH_OUTPUT_TOKENS
    assert body["input"][0]["content"] == proxy.SEARCH_SYSTEM
    assert timeout == proxy.SEARCH_TIMEOUT

    body, timeout = build_request("hi", model="grok-4.7", mode="off")
    assert "tools" not in body and "include" not in body and "max_turns" not in body
    assert body["max_output_tokens"] == proxy.PLAIN_OUTPUT_TOKENS
    assert body["input"][0]["content"] == proxy.SYSTEM
    assert timeout == proxy.PLAIN_TIMEOUT

    os.environ["XAI_SEARCH_MAX_TURNS"] = "99"
    os.environ["XAI_TIMEOUT"] = "25"
    body, timeout = build_request("hi", model="grok-4.7", mode="on")
    assert body["max_turns"] == 10 and timeout == 25
    for name in ("XAI_SEARCH_MAX_TURNS", "XAI_TIMEOUT"):
        os.environ.pop(name)


def test_fold():
    assert fold_ascii("Cafe\u00e9\nline", 640) == "Cafe?\nline"
    assert fold_ascii("abcdef", 5) == "ab..."


if __name__ == "__main__":
    test_output_text()
    test_output_blocks()
    test_error_object()
    test_empty()
    test_search_response()
    test_only_tool_items()
    test_strip_citations()
    test_search_tools_env()
    test_build_request()
    test_fold()
    print("proxy extract ok")
