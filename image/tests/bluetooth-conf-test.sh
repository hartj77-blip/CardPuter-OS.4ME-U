#!/bin/bash
# zeroos-bluetooth-conf on Debian's BlueZ main.conf (tests/data/bluez-main.conf):
# sets every option ZeroOS needs, only once, and leaves the rest alone.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CONF_SH=$ROOT/image/stage-zeroos/files/bluetooth/zeroos-bluetooth-conf
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }

cp "$ROOT/image/tests/data/bluez-main.conf" "$T/main.conf"
[ "$(sh "$CONF_SH" "$T/main.conf")" = changed ] && ok "first run edits" || bad "first run: no change reported"

# key value section: set once, uncommented, inside the right section.
check() {
    local got
    got=$(awk -v s="[$3]" -v k="$1" '/^\[/ {in_s = ($0 == s)} in_s && $0 ~ "^" k "[ \t]*=" {print}' "$T/main.conf")
    [ "$got" = "$1 = $2" ] && ok "$1 = $2 in [$3]" || bad "$1 in [$3]: '$got'"
}
check Privacy device General
check JustWorksRepairing always General
check ControllerMode dual General
check MinConnectionInterval 7 LE
check MaxConnectionInterval 9 LE
check ConnectionLatency 0 LE
check ConnectionSupervisionTimeout 200 LE

[ -z "$(sh "$CONF_SH" "$T/main.conf")" ] && ok "second run changes nothing" || bad "second run edited again"
n=$(diff "$ROOT/image/tests/data/bluez-main.conf" "$T/main.conf" | grep -c '^>')
[ "$n" = 7 ] && ok "only those 7 lines differ" || bad "$n lines differ"

# A file without an [LE] section gets one.
grep -v -e '^\[LE\]' -e 'ConnectionInterval\|ConnectionLatency\|ConnectionSupervision' \
    "$ROOT/image/tests/data/bluez-main.conf" > "$T/main.conf"
sh "$CONF_SH" "$T/main.conf" > /dev/null
check ConnectionSupervisionTimeout 200 LE
[ -z "$(sh "$CONF_SH" /nonexistent/main.conf)" ] && ok "no file: nothing to do" || bad "no file"

[ "$fails" = 0 ] && echo "all passed" || { echo "$fails failed"; exit 1; }
