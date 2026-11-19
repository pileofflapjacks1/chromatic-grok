#!/usr/bin/env python3
# Chromatic Grok proxy. Part of a GPL-3.0-or-later modified work based on the
# ModRetro Chromatic MCU firmware. See ../LICENSE and ../NOTICE.
"""LAN proxy that holds the xAI key and returns a short plain-text reply.

The Chromatic posts {"prompt": "...", "max_chars": 640} to /v1/ask.
This process calls https://api.x.ai/v1/responses and answers {"reply": "..."}.

Search: by default the request enables xAI's server-side x_search and
web_search tools so Grok can look up live X posts, accounts and the web.
XAI_SEARCH=off turns them off; XAI_SEARCH=x or XAI_SEARCH=web picks one.
Search calls are billed by xAI on top of tokens.

Security:
- Every /v1/ask request must carry "Authorization: Bearer <GROK_PROXY_TOKEN>".
  The token is compared in constant time. Anything else gets 401.
- The proxy binds 127.0.0.1 unless GROK_PROXY_ALLOW_LAN=1 is set.
- The xAI key is never returned to a client or written to the log.
"""

import argparse
import hmac
import re
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
SEARCH_SYSTEM = SYSTEM + (
    " You can search X and the web. Use search for anything live or recent, "
    "such as X accounts, posts, news or prices. "
    "Put the answer in your own words. No links, no URLs, no citation marks. "
    "Write X handles as @name."
)
SEARCH_MODES = {
    "on": ("x_search", "web_search"),
    "x": ("x_search",),
    "web": ("web_search",),
    "off": (),
}
DEFAULT_SEARCH = "on"
# Output tokens: the plain call fits 180. With search the model also writes
# tool-call arguments, so give it more room; fold_ascii still caps the text.
PLAIN_OUTPUT_TOKENS = 180
SEARCH_OUTPUT_TOKENS = 400
PLAIN_TIMEOUT = 18
SEARCH_TIMEOUT = 60
DEFAULT_MAX_TURNS = 2
DEFAULT_MODEL = "grok-4.7"
XAI_URL = "https://api.x.ai/v1/responses"
MAX_PROMPT = 2000
DEFAULT_CHARS = 640
DEFAULT_HOST = "127.0.0.1"
LAN_HOST = "0.0.0.0"
LOOPBACK_HOSTS = ("127.0.0.1", "localhost", "::1")
MIN_TOKEN_LEN = 16
_KEY_PATTERN = re.compile(r"xai-[A-Za-z0-9_\-]+")


def redact(text):
    """Remove the API key (and anything shaped like an xAI key) from text."""
    if not text:
        return text
    key = os.environ.get("XAI_API_KEY", "").strip()
    if key:
        text = text.replace(key, "[redacted]")
    return _KEY_PATTERN.sub("[redacted]", text)


def expected_token():
    return os.environ.get("GROK_PROXY_TOKEN", "").strip()


def check_auth(header_value, token=None):
    """Constant-time check of an Authorization header against the token."""
    if token is None:
        token = expected_token()
    if not token or not isinstance(header_value, str):
        return False
    prefix = "Bearer "
    if not header_value.startswith(prefix):
        return False
    given = header_value[len(prefix):].strip()
    return hmac.compare_digest(given.encode("utf-8"), token.encode("utf-8"))


def lan_allowed():
    return os.environ.get("GROK_PROXY_ALLOW_LAN", "").strip().lower() in ("1", "true", "yes")


def resolve_host(requested):
    """Loopback unless LAN access is explicitly allowed."""
    if lan_allowed():
        return requested or LAN_HOST
    if requested and requested not in LOOPBACK_HOSTS:
        raise ValueError("binding %s needs GROK_PROXY_ALLOW_LAN=1" % requested)
    return requested or DEFAULT_HOST


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


_INLINE_CITE = re.compile(r"\[\[\d+\]\]\([^)\s]*\)")
_MD_LINK = re.compile(r"\[([^\]\n]*)\]\((?:https?://|www\.)[^)\s]*\)")
_BARE_URL = re.compile(r"\(?(?:https?://|www\.)[^\s)]+\)?")
_NUM_CITE = re.compile(r"\[\d+(?:,\s*\d+)*\]")


def strip_citations(text):
    """Drop inline citations, markdown links and URLs the screen cannot use."""
    if not text:
        return text
    text = _INLINE_CITE.sub("", text)
    text = _MD_LINK.sub(lambda m: m.group(1), text)
    text = _BARE_URL.sub("", text)
    text = _NUM_CITE.sub("", text)
    text = re.sub(r"[ \t]+([.,;:!?])", r"\1", text)
    text = re.sub(r"[ \t]{2,}", " ", text)
    return "\n".join(line.strip() for line in text.split("\n")).strip()


# Output items that carry the answer. Everything else (reasoning,
# web_search_call, custom_tool_call for x_search, ...) is skipped.
_TEXT_ITEMS = ("message",)
_TEXT_BLOCKS = ("output_text", "text")


def extract_reply(payload):
    """Pull the final answer text from a Responses API body, or an error.

    Only "message" output items and their output_text blocks are used, so
    tool-call items, reasoning and citation lists never reach the screen.
    """
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
            kind = item.get("type")
            if kind is not None and kind not in _TEXT_ITEMS:
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
                        btype = block.get("type")
                        if btype is None or btype in _TEXT_BLOCKS:
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


def search_tools(mode=None):
    """Tools for XAI_SEARCH (on|off|x|web). Unknown values count as on."""
    if mode is None:
        mode = os.environ.get("XAI_SEARCH", DEFAULT_SEARCH)
    mode = (mode or DEFAULT_SEARCH).strip().lower()
    if mode in ("0", "false", "no", "none"):
        mode = "off"
    if mode in ("1", "true", "yes", "both", "all"):
        mode = "on"
    names = SEARCH_MODES.get(mode, SEARCH_MODES[DEFAULT_SEARCH])
    return [{"type": name} for name in names]


def _env_int(name, default, low, high):
    try:
        value = int(os.environ.get(name, "").strip())
    except ValueError:
        return default
    return max(low, min(high, value))


def build_request(prompt, model=None, mode=None):
    """Return (body dict, timeout seconds) for one question."""
    if model is None:
        model = os.environ.get("XAI_MODEL", DEFAULT_MODEL).strip() or DEFAULT_MODEL
    tools = search_tools(mode)
    body = {
        "model": model,
        "input": [
            {"role": "system", "content": SEARCH_SYSTEM if tools else SYSTEM},
            {"role": "user", "content": prompt},
        ],
        "max_output_tokens": SEARCH_OUTPUT_TOKENS if tools else PLAIN_OUTPUT_TOKENS,
    }
    timeout = PLAIN_TIMEOUT
    if tools:
        body["tools"] = tools
        # Caps the agentic search loop (cost and time). Citations stay out of
        # the text; the top-level citation list is ignored by extract_reply.
        body["max_turns"] = _env_int("XAI_SEARCH_MAX_TURNS", DEFAULT_MAX_TURNS, 1, 10)
        body["include"] = ["no_inline_citations"]
        timeout = SEARCH_TIMEOUT
    timeout = _env_int("XAI_TIMEOUT", timeout, 5, 300)
    return body, timeout


def ask_xai(prompt, max_chars=DEFAULT_CHARS, timeout=None):
    """Return (reply, error). One of the two is None."""
    key = os.environ.get("XAI_API_KEY", "").strip()
    if not key:
        return None, "XAI_API_KEY is not set"
    body, default_timeout = build_request(prompt)
    if timeout is None:
        timeout = default_timeout
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
        return None, redact(message)
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
        return None, "upstream unreachable"
    reply = fold_ascii(strip_citations(extract_reply(payload)), max_chars)
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
        if not check_auth(self.headers.get("Authorization")):
            self._send(401, {"error": "unauthorized"})
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
        if status == 401:
            self.send_header("WWW-Authenticate", "Bearer")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        # Request line and status only. Headers (and so the token) are not logged.
        sys.stderr.write("%s - %s\n" % (self.address_string(), redact(fmt % args)))


def main(argv):
    parser = argparse.ArgumentParser(description="Chromatic Grok LAN proxy")
    parser.add_argument("--host", default=os.environ.get("GROK_PROXY_HOST", ""),
                        help="default 127.0.0.1; a LAN address needs GROK_PROXY_ALLOW_LAN=1")
    parser.add_argument("--port", type=int, default=int(os.environ.get("GROK_PROXY_PORT", "8787")))
    args = parser.parse_args(argv)
    try:
        host = resolve_host(args.host.strip())
    except ValueError as exc:
        sys.stderr.write("%s\n" % exc)
        return 2
    token = expected_token()
    if len(token) < MIN_TOKEN_LEN:
        sys.stderr.write(
            "GROK_PROXY_TOKEN must be set (at least %d characters). Generate one with:\n"
            "  python3 -c 'import secrets; print(secrets.token_urlsafe(24))'\n" % MIN_TOKEN_LEN)
        return 2
    args.host = host
    if not os.environ.get("XAI_API_KEY", "").strip():
        sys.stderr.write("XAI_API_KEY is not set. The proxy will answer with an error until it is.\n")
    httpd = ThreadingHTTPServer((args.host, args.port), AskHandler)
    tools = [t["type"] for t in search_tools()]
    sys.stderr.write("chromatic-grok proxy on http://%s:%s/v1/ask (search: %s)\n"
                     % (args.host, args.port, ", ".join(tools) or "off"))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("\nproxy stopped\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
