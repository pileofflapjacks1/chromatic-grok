#!/bin/sh
# Host checks. These do not compile or flash the ESP32 firmware.
set -e
cd "$(dirname "$0")/.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
# Tracked sources only: do not leave __pycache__ behind.
export PYTHONDONTWRITEBYTECODE=1
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_text.c components/grok/grok_text.c components/grok/grok_pick.c \
    -o "$out/test_grok_text"
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_ota.c components/grok/grok_ota_util.c \
    -o "$out/test_grok_ota"
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_web.c components/grok/grok_web_util.c \
    -o "$out/test_grok_web"
"$out/test_grok_ota"
"$out/test_grok_web"
"$out/test_grok_text"
python3 tests/test_proxy_extract.py
python3 tests/test_proxy_auth.py
python3 -c 'import ast, sys
for path in sys.argv[1:]:
    with open(path, encoding="utf-8") as handle:
        ast.parse(handle.read(), path)' companion/proxy.py companion/chromatic_grok.py
