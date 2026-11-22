#!/usr/bin/env bash
# Unit tests that need no LVGL and no device.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/keys_test.cpp" "$HERE/../src/platform/keys.cpp" -o "$OUT/keys_test"
"$OUT/keys_test"
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/jellyfin_test.cpp" "$HERE/../src/hal/jellyfin.cpp" -o "$OUT/jellyfin_test"
"$OUT/jellyfin_test"
