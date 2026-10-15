#!/usr/bin/env python3
# Chromatic Grok proxy. Part of a GPL-3.0-or-later modified work based on the
# ModRetro Chromatic MCU firmware. See ../LICENSE and ../NOTICE.
"""LAN proxy that holds the xAI key and returns a short plain-text reply.

The Chromatic posts {"prompt": "...", "max_chars": 640} to /v1/ask.
This process calls https://api.x.ai/v1/responses and answers {"reply": "..."}.
There is no authentication. Run it only on a network you trust.
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SYSTEM = (
    "You are answering on a 160x144 handheld with a 5px font. "
    "Reply in short plain ASCII. No markdown, no lists, no emoji. "
    "A few sentences at most."
)
DEFAULT_MODEL = "grok-4.7"
XAI_URL = "https://api.x.ai/v1/responses"
MAX_PROMPT = 2000
DEFAULT_CHARS = 640


def fold_ascii(text, max_chars):
    """Keep newlines and printable ASCII. Cap the length for the small screen."""
    if text is None:
        text = ""
    out = []
    for ch in text:
        code = ord(ch)
        if ch == "\n" or 32 <= code <= 126:
            out.append(ch)
        elif ch == "\t":
            out.append(" ")
        elif ch == "\r":
            if not out or out[-1] != "\n":
                out.append("\n")
        else:
            out.append("?")
    folded = "".join(out).strip()
    try:
        limit = int(max_chars)
    except (TypeError, ValueError):
        limit = DEFAULT_CHARS
    if limit < 1:
        limit = 1
    if limit > DEFAULT_CHARS:
        limit = DEFAULT_CHARS
    if len(folded) > limit:
        if limit >= 3:
            folded = folded[: limit - 3].rstrip() + "..."
        else:
            folded = folded[:limit]
    return folded


def extract_reply(payload):
    """Pull plain text from a Responses API body, or from an error object."""
    if not isinstance(payload, dict):
        return ""

    direct = payload.get("output_text")
    if isinstance(direct, str) and direct.strip():
        return direct.strip()

    parts = []
    output = payload.get("output")
    if isinstance(output, list):
        for item in output:
            if isinstance(item, str):
                parts.append(item)
                continue
            if not isinstance(item, dict):
                continue
            content = item.get("content")
            if isinstance(content, str):
                parts.append(content)
                continue
            if isinstance(content, list):
                for block in content:
                    if isinstance(block, str):
                        parts.append(block)
                    elif isinstance(block, dict) and isinstance(block.get("text"), str):
                        parts.append(block["text"])
    joined = "\n".join(part.strip() for part in parts if isinstance(part, str) and part.strip()).strip()
    if joined:
        return joined

    err = payload.get("error")
    if isinstance(err, dict):
        message = err.get("message") or err.get("code") or ""
        return str(message).strip()
    if isinstance(err, str):
        return err.strip()
    return ""


def ask_xai(prompt, max_chars=DEFAULT_CHARS, timeout=18):
    """Return (reply, error). One of the two is None."""
    key = os.environ.get("XAI_API_KEY", "").strip()
    if not key:
        return None, "XAI_API_KEY is not set"
    model = os.environ.get("XAI_MODEL", DEFAULT_MODEL).strip() or DEFAULT_MODEL
    body = {
        "model": model,
        "input": [
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": prompt},
        ],
        "max_output_tokens": 180,
    }
    req = urllib.request.Request(
        XAI_URL,
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Authorization": "Bearer " + key,
            "Content-Type": "application/json",
            "User-Agent": "chromatic-grok-proxy",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", "replace")
        payload = json.loads(raw)
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", "replace")
        try:
            payload = json.loads(raw)
        except json.JSONDecodeError:
            return None, "upstream http %s" % exc.code
        message = extract_reply(payload) or ("upstream http %s" % exc.code)
        return None, message
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
        return None, "upstream unreachable"
    reply = fold_ascii(extract_reply(payload), max_chars)
    if not reply:
        return None, "empty reply"
    return reply, None


class AskHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/health":
            self._send(200, {"ok": True})
            return
        self._send(404, {"error": "not found"})

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path not in ("/v1/ask", "/"):
            self._send(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length < 0 or length > 8192:
            self._send(400, {"error": "body too large"})
            return
        raw = self.rfile.read(length).decode("utf-8", "replace") if length else ""
        try:
            incoming = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            self._send(400, {"error": "body must be json"})
            return
        if not isinstance(incoming, dict):
            self._send(400, {"error": "body must be a json object"})
            return
        prompt = incoming.get("prompt", "")
        if not isinstance(prompt, str) or not prompt.strip():
            self._send(400, {"error": "empty prompt"})
            return
        if len(prompt) > MAX_PROMPT:
            self._send(400, {"error": "prompt too long"})
            return
        reply, err = ask_xai(prompt.strip(), incoming.get("max_chars", DEFAULT_CHARS))
        if err:
            self._send(502, {"error": err})
            return
        self._send(200, {"reply": reply})

    def _send(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main(argv):
    parser = argparse.ArgumentParser(description="Chromatic Grok LAN proxy")
    parser.add_argument("--host", default=os.environ.get("GROK_PROXY_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("GROK_PROXY_PORT", "8787")))
    args = parser.parse_args(argv)
    if not os.environ.get("XAI_API_KEY", "").strip():
        sys.stderr.write("XAI_API_KEY is not set. The proxy will answer with an error until it is.\n")
    httpd = ThreadingHTTPServer((args.host, args.port), AskHandler)
    sys.stderr.write("chromatic-grok proxy on http://%s:%s/v1/ask\n" % (args.host, args.port))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("\nproxy stopped\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
