#!/usr/bin/env bash
# Tests for releasing only some apps (docs/APPS.md): image/apps/merge-index.py
# takes the listed apps from this commit's build and keeps the rest as they
# were in the previous release. Fake packages; nothing is built.
#   image/tests/release-apps-test.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MERGE=$ROOT/image/apps/merge-index.py
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }

# index <dir> <version> <abi> <app>...: an index and a package for each app
index() {
    local dir=$1 v=$2 abi=$3; shift 3
    mkdir -p "$dir"
    { echo "# ZeroOS apps $v (docs/APPS.md). Read by zeroos-apps on the device."; echo
      for a in "$@"; do
          echo "zeroos-app-$a $v" > "$dir/zeroos-app-${a}_${v}_arm64.deb"
          printf 'app=%s\nname=%s\nversion=%s\nabi=%s\nfile=zeroos-app-%s_%s_arm64.deb\nsha256=x\nsize=1\n\n' \
              "$a" "$a" "$v" "$abi" "$a" "$v"
      done; } > "$dir/zeroos-apps.index"
}
ver() { awk -v a="$2" '/^app=/{x=($0=="app="a)} x&&/^version=/{sub("version=","");print}' "$1/zeroos-apps.index"; }

index "$T/old" 0.4.0 2 notes media tether
index "$T/new" 0.4.0+apps.1 2 notes media tether router

python3 "$MERGE" "$T/new" "$T/old" "$T/out" router > "$T/report"
[ "$(ver "$T/out" router)" = 0.4.0+apps.1 ] && ok "listed new app is published" || bad "router not published"
[ "$(ver "$T/out" notes)" = 0.4.0 ] && ok "unlisted app keeps its old version" || bad "notes changed"
[ -f "$T/out/zeroos-app-notes_0.4.0_arm64.deb" ] && ok "old package carried over" || bad "old package missing"
[ ! -e "$T/out/zeroos-app-notes_0.4.0+apps.1_arm64.deb" ] && ok "unlisted app's new build left out" || bad "new notes build published"
[ "$(grep -c '^app=' "$T/out/zeroos-apps.index")" = 4 ] && ok "four apps in the index" || bad "index count"
head -1 "$T/out/zeroos-apps.index" | grep -q '^# ZeroOS apps' && ok "index keeps its header" || bad "header"

rm -rf "$T/out"
python3 "$MERGE" "$T/new" "$T/old" "$T/out" media > /dev/null
! grep -q '^app=router' "$T/out/zeroos-apps.index" && ok "unreleased app stays unpublished" || bad "router published"
[ "$(ver "$T/out" media)" = 0.4.0+apps.1 ] && ok "listed existing app gets its update" || bad "media not updated"

rm -rf "$T/out"
python3 "$MERGE" "$T/new" "$T/old" "$T/out" all > /dev/null
[ "$(grep -c '^version=0.4.0+apps.1' "$T/out/zeroos-apps.index")" = 4 ] && ok "all: every app new" || bad "all"

python3 "$MERGE" "$T/new" "$T/old" "$T/out2" nosuchapp > /dev/null 2>&1 && bad "unknown app accepted" || ok "unknown app refused"

index "$T/new3" 0.5.0 3 notes media tether router
python3 "$MERGE" "$T/new3" "$T/old" "$T/out3" router > /dev/null 2> "$T/err" && bad "ABI change carried old apps" \
    || { grep -q "app ABI" "$T/err" && ok "ABI change: refuses to carry old apps"; }

[ "$fails" = 0 ] && echo "all release tests passed" || { echo "$fails failed"; exit 1; }
