#!/usr/bin/env python3
# Chromatic Grok Bot reply relay. GPL-3.0-or-later. See ../LICENSE and ../NOTICE.
"""Minimal reply mailbox for Chromatic <-> Grok Bot routines.

Bots' routines POST a short reply + 3 suggestions.
The Chromatic polls GET until the reply appears (or times out on-device).

Env (required in production):
  BOT_POST_TOKEN   Bearer token for POST /v1/reply
  DEVICE_TOKEN     Bearer token for GET /v1/reply
Optional:
  RELAY_STORE      Path to JSON file store (default: ./relay_store.json)
  RELAY_HOST       Bind host (default 127.0.0.1; set 0.0.0.0 behind grok.me)
  RELAY_PORT       Port (default 8787)
  RELAY_TTL_SEC    Message TTL (default 3600)
"""

from __future__ import annotations

import argparse
import hmac
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

MAX_REPLY = 40
MAX_SUGGEST = 17
MAX_SUGGESTIONS = 3
MAX_ID = 64
MAX_BODY = 4096
RATE_LIMIT = 60  # requests per window per token class
RATE_WINDOW = 60.0
DEFAULT_TTL = 3600


def _env(name: str) -> str:
    return os.environ.get(name, "").strip()


def check_auth(header: str | None, expected: str) -> bool:
    if not expected or not isinstance(header, str):
        return False
    prefix = "Bearer "
    if not header.startswith(prefix):
        return False
    given = header[len(prefix) :].strip()
    return hmac.compare_digest(given.encode("utf-8"), expected.encode("utf-8"))


def fold_ascii(text: str, limit: int) -> str:
    if text is None:
        return ""
    out = []
    for ch in text:
        code = ord(ch)
        if 32 <= code <= 126:
            out.append(ch)
        elif ch in "\n\r\t":
            out.append(" ")
        else:
            out.append("?")
    return "".join(out).strip()[:limit]


class Store:
    def __init__(self, path: Path, ttl: int):
        self.path = path
        self.ttl = ttl
        self.lock = threading.Lock()
        self.data: dict = {"messages": {}}
        self._load()

    def _load(self) -> None:
        if not self.path.exists():
            return
        try:
            self.data = json.loads(self.path.read_text(encoding="utf-8"))
            if "messages" not in self.data:
                self.data = {"messages": {}}
        except (OSError, json.JSONDecodeError):
            self.data = {"messages": {}}

    def _save(self) -> None:
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.data), encoding="utf-8")
        tmp.replace(self.path)

    def _key(self, device_id: str, msg_id: str) -> str:
        return f"{device_id}|{msg_id}"

    def _purge(self, now: float) -> None:
        dead = [
            k
            for k, v in self.data["messages"].items()
            if (now - float(v.get("ts", 0))) > self.ttl
        ]
        for k in dead:
            del self.data["messages"][k]

    def put(self, device_id: str, msg_id: str, reply: str, suggestions: list[str]) -> None:
        now = time.time()
        with self.lock:
            self._purge(now)
            self.data["messages"][self._key(device_id, msg_id)] = {
                "reply": reply,
                "suggestions": suggestions,
                "ts": now,
            }
            self._save()

    def get(self, device_id: str, msg_id: str):
        now = time.time()
        with self.lock:
            self._purge(now)
            return self.data["messages"].get(self._key(device_id, msg_id))


class RateLimiter:
    def __init__(self):
        self.lock = threading.Lock()
        self.hits: dict[str, list[float]] = {}

    def allow(self, key: str) -> bool:
        now = time.time()
        with self.lock:
            bucket = [t for t in self.hits.get(key, []) if (now - t) < RATE_WINDOW]
            if len(bucket) >= RATE_LIMIT:
                self.hits[key] = bucket
                return False
            bucket.append(now)
            self.hits[key] = bucket
            return True


def make_handler(store: Store, limiter: RateLimiter):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):  # quieter
            pass

        def _read_json(self):
            length = int(self.headers.get("Content-Length", "0") or "0")
            if length <= 0 or length > MAX_BODY:
                return None
            raw = self.rfile.read(length)
            try:
                return json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                return None

        def _send(self, code: int, body=None, content_type="application/json"):
            data = b""
            if body is not None:
                if isinstance(body, (dict, list)):
                    data = json.dumps(body).encode("utf-8")
                elif isinstance(body, str):
                    data = body.encode("utf-8")
                else:
                    data = body
            self.send_response(code)
            if data:
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if data:
                self.wfile.write(data)

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path != "/v1/reply":
                if parsed.path in ("/", "/health"):
                    self._send(200, {"ok": True, "service": "chromatic-grok-relay"})
                    return
                self._send(404, {"error": "not found"})
                return
            token = _env("DEVICE_TOKEN")
            if not check_auth(self.headers.get("Authorization"), token):
                self._send(401, {"error": "unauthorized"})
                return
            if not limiter.allow("get"):
                self._send(429, {"error": "rate limited"})
                return
            qs = parse_qs(parsed.query)
            device_id = fold_ascii((qs.get("device_id") or [""])[0], MAX_ID)
            msg_id = fold_ascii((qs.get("msg_id") or [""])[0], MAX_ID)
            if not device_id or not msg_id:
                self._send(400, {"error": "device_id and msg_id required"})
                return
            item = store.get(device_id, msg_id)
            if item is None:
                self._send(204)
                return
            self._send(
                200,
                {
                    "reply": item["reply"],
                    "suggestions": item.get("suggestions") or [],
                },
            )

        def do_POST(self):
            parsed = urlparse(self.path)
            if parsed.path != "/v1/reply":
                self._send(404, {"error": "not found"})
                return
            token = _env("BOT_POST_TOKEN")
            if not check_auth(self.headers.get("Authorization"), token):
                self._send(401, {"error": "unauthorized"})
                return
            if not limiter.allow("post"):
                self._send(429, {"error": "rate limited"})
                return
            body = self._read_json()
            if not isinstance(body, dict):
                self._send(400, {"error": "invalid json"})
                return
            device_id = fold_ascii(str(body.get("device_id") or ""), MAX_ID)
            msg_id = fold_ascii(str(body.get("msg_id") or ""), MAX_ID)
            reply = fold_ascii(str(body.get("reply") or ""), MAX_REPLY)
            raw_sug = body.get("suggestions") or []
            if not device_id or not msg_id or not reply:
                self._send(400, {"error": "device_id, msg_id, reply required"})
                return
            if not isinstance(raw_sug, list):
                self._send(400, {"error": "suggestions must be a list"})
                return
            suggestions = []
            for s in raw_sug[:MAX_SUGGESTIONS]:
                clipped = fold_ascii(str(s), MAX_SUGGEST)
                if clipped:
                    suggestions.append(clipped)
            while len(suggestions) < MAX_SUGGESTIONS:
                suggestions.append("...")
            store.put(device_id, msg_id, reply, suggestions[:MAX_SUGGESTIONS])
            self._send(200, {"ok": True})

    return Handler


def main(argv=None):
    parser = argparse.ArgumentParser(description="Chromatic Grok Bot reply relay")
    parser.add_argument("--host", default=os.environ.get("RELAY_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("RELAY_PORT", "8787")))
    parser.add_argument(
        "--store",
        default=os.environ.get("RELAY_STORE", "relay_store.json"),
    )
    parser.add_argument(
        "--ttl",
        type=int,
        default=int(os.environ.get("RELAY_TTL_SEC", str(DEFAULT_TTL))),
    )
    args = parser.parse_args(argv)

    if not _env("BOT_POST_TOKEN") or not _env("DEVICE_TOKEN"):
        raise SystemExit("BOT_POST_TOKEN and DEVICE_TOKEN env vars are required")

    store = Store(Path(args.store), args.ttl)
    limiter = RateLimiter()
    server = ThreadingHTTPServer((args.host, args.port), make_handler(store, limiter))
    print(f"relay listening on http://{args.host}:{args.port} store={args.store}")
    server.serve_forever()


if __name__ == "__main__":
    main()
