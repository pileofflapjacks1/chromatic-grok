#!/bin/sh
# Host checks. These do not compile or flash the ESP32 firmware.
set -e
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -I components/grok \
    tests/test_grok_text.c components/grok/grok_text.c components/grok/grok_pick.c \
    -o /tmp/test_grok_text
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_ota.c components/grok/grok_ota_util.c \
    -o /tmp/test_grok_ota
/tmp/test_grok_ota
/tmp/test_grok_text
python3 tests/test_proxy_extract.py
python3 tests/test_proxy_auth.py
python3 -m py_compile companion/proxy.py companion/chromatic_grok.py
