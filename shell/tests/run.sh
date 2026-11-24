#!/usr/bin/env bash
# Unit tests that need no LVGL and no device.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/keys_test.cpp" "$HERE/../src/platform/keys.cpp" -o "$OUT/keys_test"
"$OUT/keys_test"
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/jellyfin_test.cpp" "$HERE/../src/hal/jellyfin.cpp" "$HERE/../src/platform/watchdog.cpp" -o "$OUT/jellyfin_test"
"$OUT/jellyfin_test"
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/tailscale_up_test.cpp" -o "$OUT/tailscale_up_test"
"$OUT/tailscale_up_test"
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/search_match_test.cpp" "$HERE/../src/ui/search_match.cpp" -o "$OUT/search_match_test"
"$OUT/search_match_test"
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/suggest_test.cpp" "$HERE/../src/ui/suggest.cpp" "$HERE/../src/ui/suggest_words.cpp" "$HERE/../src/ui/fileio.cpp" -o "$OUT/suggest_test"
"$OUT/suggest_test"
