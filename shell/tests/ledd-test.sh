#!/usr/bin/env bash
# Test for zeroos-grove-ledd (docs/GROVE.md) with no hardware: start it with
# --dry-run on a socket in a temporary folder, talk to it like the Grove app
# does, and check its replies and what it would send to the strip.
#   shell/tests/ledd-test.sh <path to zeroos-grove-ledd>
set -euo pipefail
D=$1
T=$(mktemp -d)
trap 'kill "${PID:-0}" 2>/dev/null || true; rm -rf -- "${T:?}"' EXIT
"$D" --dry-run --socket "$T/s" > "$T/out" 2> "$T/err" &
PID=$!
for _ in $(seq 50); do [ -S "$T/s" ] && break; sleep 0.1; done
[ -S "$T/s" ] || { echo "FAIL no socket"; cat "$T/err"; exit 1; }

ask() {
    python3 -I - "$T/s" "$@" <<'PY'
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
f = s.makefile("rw")
for line in sys.argv[2:]:
    f.write(line + "\n"); f.flush()
    print(f.readline().rstrip("\n"))
PY
}
fails=0
expect() {   # expect <what> <pattern> <reply>
    if [[ $3 == $2 ]]; then echo "ok   $1"; else echo "FAIL $1: got '$3'"; fails=$((fails + 1)); fi
}

expect "starts off" "ok effect=off * hardware=off" "$(ask status)"
expect "sets an effect" "ok effect=solid color=ff0000 brightness=100 speed=5 count=3 hardware=*" \
    "$(ask 'set effect=solid color=ff0000 brightness=100 count=3')"
sleep 0.3
grep -q "strip: ff0000 ff0000 ff0000" "$T/out" && echo "ok   lights the strip" || { echo "FAIL strip: $(cat "$T/out")"; fails=$((fails + 1)); }
expect "hardware on" "*hardware=on" "$(ask status)"
expect "refuses a bad value" "error brightness must be 1 to 100" "$(ask 'set brightness=0')"
expect "refuses a bad command" "error unknown command*" "$(ask 'dance')"
expect "keeps settings between clients" "ok effect=solid color=ff0000*" "$(ask status)"
ask 'set effect=pixels pixels=010203,0a0b0c' > /dev/null
sleep 0.3
grep -q "strip: 010203 0a0b0c 000000" "$T/out" && echo "ok   pixels one by one" || { echo "FAIL pixels: $(tail -1 "$T/out")"; fails=$((fails + 1)); }
expect "turns off" "ok effect=off*" "$(ask off)"
sleep 2.8
tail -2 "$T/out" | grep -q "strip: 000000 000000 000000" && tail -1 "$T/out" | grep -q "strip: closed" &&
    echo "ok   off: dark, then lets the pins go" || { echo "FAIL off: $(tail -3 "$T/out")"; fails=$((fails + 1)); }
kill "$PID"
wait "$PID" || true
[ ! -e "$T/s" ] && echo "ok   removes its socket on exit" || { echo "FAIL socket left behind"; fails=$((fails + 1)); }
[ "$fails" -eq 0 ]
