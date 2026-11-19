"""Host checks for proxy auth, binding and key redaction. No network to xAI."""

import json
import os
import sys
import threading
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "companion"))

import proxy  # noqa: E402

TOKEN = "test-token-0123456789abcdef"
FAKE_KEY = "xai-FAKEKEYFORTESTS000000000000"


def _post(port, headers):
    req = urllib.request.Request(
        "http://127.0.0.1:%d/v1/ask" % port,
        data=json.dumps({"prompt": "hi"}).encode(),
        headers=dict({"Content-Type": "application/json"}, **headers),
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status, resp.read().decode()
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode()


def test_check_auth():
    assert proxy.check_auth("Bearer " + TOKEN, TOKEN)
    assert not proxy.check_auth("Bearer wrong", TOKEN)
    assert not proxy.check_auth(TOKEN, TOKEN)
    assert not proxy.check_auth(None, TOKEN)
    assert not proxy.check_auth("Bearer ", "")


def test_resolve_host():
    os.environ.pop("GROK_PROXY_ALLOW_LAN", None)
    assert proxy.resolve_host("") == "127.0.0.1"
    assert proxy.resolve_host("127.0.0.1") == "127.0.0.1"
    try:
        proxy.resolve_host("0.0.0.0")
        raise AssertionError("LAN bind must need opt-in")
    except ValueError:
        pass
    os.environ["GROK_PROXY_ALLOW_LAN"] = "1"
    assert proxy.resolve_host("") == "0.0.0.0"
    assert proxy.resolve_host("192.168.1.20") == "192.168.1.20"
    os.environ.pop("GROK_PROXY_ALLOW_LAN")


def test_main_refuses_without_token():
    os.environ.pop("GROK_PROXY_TOKEN", None)
    assert proxy.main(["--port", "0"]) == 2
    os.environ["GROK_PROXY_TOKEN"] = "short"
    assert proxy.main(["--port", "0"]) == 2
    os.environ.pop("GROK_PROXY_TOKEN")


def test_redact():
    os.environ["XAI_API_KEY"] = FAKE_KEY
    out = proxy.redact("Incorrect API key provided: " + FAKE_KEY)
    assert FAKE_KEY not in out and "[redacted]" in out
    assert "xai-" not in proxy.redact("key xai-abc123 bad")


def test_http_401_and_no_key_leak():
    os.environ["GROK_PROXY_TOKEN"] = TOKEN
    os.environ["XAI_API_KEY"] = FAKE_KEY
    calls = []

    def fake_ask(prompt, max_chars=640, timeout=None):
        calls.append(prompt)
        # Simulate xAI echoing the key in an error.
        return None, proxy.redact("Incorrect API key provided: " + FAKE_KEY)

    real = proxy.ask_xai
    proxy.ask_xai = fake_ask
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), proxy.AskHandler)
    port = httpd.server_address[1]
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        status, body = _post(port, {})
        assert status == 401, status
        status, body = _post(port, {"Authorization": "Bearer nope"})
        assert status == 401, status
        assert not calls
        status, body = _post(port, {"Authorization": "Bearer " + TOKEN})
        assert status == 502, status
        assert FAKE_KEY not in body
        assert calls == ["hi"]
    finally:
        httpd.shutdown()
        proxy.ask_xai = real


class _FakeResp:
    def __init__(self, payload):
        self._raw = json.dumps(payload).encode()

    def read(self):
        return self._raw

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def test_ask_xai_search_request():
    os.environ["XAI_API_KEY"] = FAKE_KEY
    os.environ.pop("XAI_SEARCH", None)
    os.environ.pop("XAI_TIMEOUT", None)
    seen = {}

    def fake_urlopen(req, timeout=None):
        seen["body"] = req.data.decode()
        seen["auth"] = req.get_header("Authorization")
        seen["timeout"] = timeout
        return _FakeResp({"output": [
            {"type": "custom_tool_call", "call_id": "xs_9", "input": "{\"query\": \"from:CardCaptain\"}"},
            {"type": "message", "content": [{"type": "output_text",
                                             "text": "@CardCaptain is a card collector [[1]](https://x.com/c/1).",
                                             "annotations": [{"type": "url_citation", "url": "https://x.com/c/1"}]}]},
        ], "citations": ["https://x.com/c/1"]})

    real = proxy.urllib.request.urlopen
    proxy.urllib.request.urlopen = fake_urlopen
    try:
        reply, err = proxy.ask_xai("Who is @CardCaptain on X")
        assert err is None, err
        assert reply == "@CardCaptain is a card collector.", reply
        body = json.loads(seen["body"])
        assert [t["type"] for t in body["tools"]] == ["x_search", "web_search"]
        assert FAKE_KEY not in seen["body"]
        assert seen["auth"] == "Bearer " + FAKE_KEY
        assert seen["timeout"] == proxy.SEARCH_TIMEOUT

        os.environ["XAI_SEARCH"] = "off"
        reply, err = proxy.ask_xai("hi")
        assert "tools" not in json.loads(seen["body"])
        assert seen["timeout"] == proxy.PLAIN_TIMEOUT
    finally:
        proxy.urllib.request.urlopen = real
        os.environ.pop("XAI_SEARCH", None)


if __name__ == "__main__":
    import io
    err = io.StringIO()
    real_err = sys.stderr
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for fn in tests:
        sys.stderr = err
        try:
            fn()
        finally:
            sys.stderr = real_err
    assert FAKE_KEY not in err.getvalue(), "key leaked to log"
    print("proxy auth: %d tests ok" % len(tests))
