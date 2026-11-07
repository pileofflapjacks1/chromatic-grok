#!/bin/sh
# Compile the pinned ESP-IDF v5.3 splitter. No network.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
src="$root/tests/vendor/esp-idf-v5.3/split_argv.c"
main="$root/tests/usb_split_argv_main.c"
out="${TMPDIR:-/tmp}/chromatic-usb-split-argv"
expected=9df84ac7f41845c21260a3062480bb93a6969a844e984461242ce459093bcc91
actual=$(sha256sum "$src" | awk 'NR==1 { print $1 }')
if [ "$actual" != "$expected" ]; then
    echo "split_argv.c SHA256 mismatch: $actual" >&2
    exit 1
fi
cc -std=c11 -Wall -Wextra -Werror -O2 -o "$out" "$main" "$src"
printf '%s\n' "$out"
