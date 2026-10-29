#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host tests for relay/app.py (no network server required for unit bits)."""

import json
import os
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "relay"))

os.environ["BOT_POST_TOKEN"] = "bot-token-for-tests-012345"
os.environ["DEVICE_TOKEN"] = "device-token-for-tests-012345"

import app as relay  # noqa: E402


def expect(cond, msg):
    if not cond:
        raise AssertionError(msg)


def test_fold_and_auth():
    expect(relay.fold_ascii("hi\nthere", 40) == "hi there", "fold")
    expect(relay.check_auth("Bearer bot-token-for-tests-012345", os.environ["BOT_POST_TOKEN"]), "auth ok")
    expect(not relay.check_auth("Bearer wrong", os.environ["BOT_POST_TOKEN"]), "auth bad")
    expect(not relay.check_auth(None, os.environ["BOT_POST_TOKEN"]), "auth none")


def test_store_ttl(tmp_path=None):
    with tempfile.TemporaryDirectory() as td:
        store = relay.Store(Path(td) / "s.json", ttl=1)
        store.put("dev", "m1", "Hello reply", ["A", "B", "C"])
        got = store.get("dev", "m1")
        expect(got is not None and got["reply"] == "Hello reply", "get")
        expect(got["suggestions"] == ["A", "B", "C"], "sug")
        store.data["messages"]["dev|m1"]["ts"] = time.time() - 10
        expect(store.get("dev", "m1") is None, "expired")


def test_http_roundtrip():
    with tempfile.TemporaryDirectory() as td:
        store = relay.Store(Path(td) / "s.json", ttl=3600)
        limiter = relay.RateLimiter()
        server = relay.ThreadingHTTPServer(("127.0.0.1", 0), relay.make_handler(store, limiter))
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            # empty get -> 204
            req = urllib.request.Request(
                f"http://127.0.0.1:{port}/v1/reply?device_id=d1&msg_id=m1",
                headers={"Authorization": "Bearer device-token-for-tests-012345"},
            )
            with urllib.request.urlopen(req, timeout=2) as resp:
                expect(resp.status == 204, f"got {resp.status}")

            body = json.dumps(
                {
                    "device_id": "d1",
                    "msg_id": "m1",
                    "reply": "Done.",
                    "suggestions": ["Ship PR", "Add test", "Nap"],
                }
            ).encode()
            req = urllib.request.Request(
                f"http://127.0.0.1:{port}/v1/reply",
                data=body,
                headers={
                    "Authorization": "Bearer bot-token-for-tests-012345",
                    "Content-Type": "application/json",
                },
                method="POST",
            )
            with urllib.request.urlopen(req, timeout=2) as resp:
                expect(resp.status == 200, "post")

            req = urllib.request.Request(
                f"http://127.0.0.1:{port}/v1/reply?device_id=d1&msg_id=m1",
                headers={"Authorization": "Bearer device-token-for-tests-012345"},
            )
            with urllib.request.urlopen(req, timeout=2) as resp:
                data = json.loads(resp.read().decode())
                expect(data["reply"] == "Done.", "reply")
                expect(data["suggestions"][0] == "Ship PR", "sug0")

            # bad auth
            req = urllib.request.Request(
                f"http://127.0.0.1:{port}/v1/reply?device_id=d1&msg_id=m1",
                headers={"Authorization": "Bearer nope"},
            )
            try:
                urllib.request.urlopen(req, timeout=2)
                raise AssertionError("expected 401")
            except urllib.error.HTTPError as e:
                expect(e.code == 401, "401")
        finally:
            server.shutdown()


if __name__ == "__main__":
    test_fold_and_auth()
    test_store_ttl()
    test_http_roundtrip()
    print("relay ok")
