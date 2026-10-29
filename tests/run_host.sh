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
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_web.c components/grok/grok_web_util.c \
    -o /tmp/test_grok_web
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_bots.c components/grok/grok_bots.c \
    -o /tmp/test_grok_bots
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_loader.c components/grok/grok_loader.c components/grok/grok_loader_frames.c \
    -o /tmp/test_grok_loader
cc -std=c11 -Wall -Wextra -Werror -I components/grok \
    tests/test_grok_bot_proto.c components/grok/grok_bot_proto.c components/grok/grok_text.c \
    -o /tmp/test_grok_bot_proto
/tmp/test_grok_ota
/tmp/test_grok_web
/tmp/test_grok_text
/tmp/test_grok_bots
/tmp/test_grok_loader
/tmp/test_grok_bot_proto
python3 tests/test_proxy_extract.py
python3 tests/test_proxy_auth.py
python3 -m py_compile companion/proxy.py companion/chromatic_grok.py
python3 -m py_compile relay/app.py
python3 tests/test_relay.py
