#!/usr/bin/env bash
# Smoke test for installable apps (docs/APPS.md) in a headless build made with
# -DZS_APPS_AS_MODULES=ON: ZeroShell starts with no apps installed, then
# loads every module in <build>/apps and opens each app. Fails if a module
# won't load (a missing symbol, a wrong ABI) or an app crashes.
#   shell/tools/modules-test.sh <build-dir> [app-id...]
# Without ids, each module's name is taken as its one app's id.
set -euo pipefail
B=$(realpath "$1")
T=$(mktemp -d)
trap 'rm -rf -- "${T:?}"' EXIT
shift
ids=${*:-$(cd "$B/apps" && ls ./*.so | sed 's|^\./||; s|\.so$||')}
[ -n "$ids" ] || { echo "FAIL no modules in $B/apps"; exit 1; }
mkdir -p "$T/empty" "$T/home"
cd "$T"
HOME=$T/home ZS_APP_DIR=$T/empty "$B/zeroshell" --script "shot:none.ppm,open:apps,wait:500,shot:store.ppm" > /dev/null
echo "ok   starts with no apps installed"
steps="shot:all.ppm"
for id in $ids; do steps+=",open:$id,wait:600,shot:$id.ppm"; done
HOME=$T/home ZS_APP_DIR=$B/apps "$B/zeroshell" --script "$steps,home" > /dev/null 2> "$T/err" || {
    echo "FAIL zeroshell exited with an error"; cat "$T/err"; exit 1; }
if grep -q 'app module' "$T/err"; then
    echo "FAIL a module didn't load:"; grep 'app module' "$T/err"; exit 1
fi
for id in $ids; do
    grep -q "^zeroshell: loaded app $id from " "$T/err" || { echo "FAIL $id didn't load"; cat "$T/err"; exit 1; }
    [ -s "$T/$id.ppm" ] || { echo "FAIL no screen for $id"; exit 1; }
done
echo "ok   every module loads and opens: $(echo $ids | tr '\n' ' ')"
