#!/usr/bin/env bash
# Build every installable app (docs/APPS.md): one zeroos-app-<id>.deb per
# image/apps/<id>.pkg, and zeroos-apps.index, which the Apps app reads.
# A release publishes them next to the zeroos package.
#
# Usage: image/apps/build.sh [modules-dir] [out-dir]
#   modules-dir: the arm64 device build's apps/ (default build/device/apps)
# Env:   ZEROOS_VERSION (default: image/version.sh)
#        ZEROOS_IRDB: a built IR code database (default: build it from Flipper-IRDB)
#        ZEROOS_APP_KEYS: a folder with tailscale.gpg and meshtastic.asc (tests; skips the download)
# Needs: dpkg-deb, curl, gpg (for the Tailscale and Meshtastic repository keys), python3, git.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
MODS=${1:-$ROOT/build/device/apps}
OUT=${2:-$ROOT/build/apps}
VERSION=${ZEROOS_VERSION:-$("$ROOT/image/version.sh")}
# The repository keys apps need, pinned where the repository is a beta (build.sh).
MESHTASTIC_KEY_FPR=$(sed -n 's/^MESHTASTIC_KEY_FPR=\([0-9A-F]*\).*/\1/p' "$ROOT/image/build.sh")
ABI=$(sed -n 's/^#define ZS_APP_ABI \([0-9]*\).*/\1/p' "$ROOT/shell/src/ui/app.h")
[ -n "$ABI" ] || { echo "error: no ZS_APP_ABI in shell/src/ui/app.h" >&2; exit 1; }

KEYS=$(mktemp -d)
trap 'rm -rf -- "${KEYS:?}"' EXIT
export KEYS
if [ -n "${ZEROOS_APP_KEYS:-}" ]; then
    # Tests: keys from a folder (tailscale.gpg, meshtastic.asc), not the network.
    cp "$ZEROOS_APP_KEYS"/tailscale.gpg "$ZEROOS_APP_KEYS"/meshtastic.asc "$KEYS/"
else
    curl -fsSL --retry 3 -o "$KEYS/tailscale.gpg" https://pkgs.tailscale.com/stable/debian/trixie.noarmor.gpg
    curl -fsSL --retry 3 -o "$KEYS/meshtastic.asc" \
        https://download.opensuse.org/repositories/network:/Meshtastic:/beta/Debian_13/Release.key
    fpr=$(gpg --show-keys --with-colons "$KEYS/meshtastic.asc" | awk -F: '$1 == "fpr" {print $10; exit}')
    [ "$fpr" = "$MESHTASTIC_KEY_FPR" ] || { echo "error: Meshtastic key is $fpr, expected $MESHTASTIC_KEY_FPR" >&2; exit 1; }
fi

# IR Remote's code database (docs/IR.md), from Flipper-IRDB.
export ZEROOS_IRDB=${ZEROOS_IRDB:-$ROOT/build/irdb}
if [ ! -f "$ZEROOS_IRDB/index.tsv" ]; then
    python3 -I "$ROOT/tools/irdb/build_irdb.py" --fetch --cache "$ROOT/build/irdb-src" --out "$ZEROOS_IRDB"
fi

mkdir -p "$OUT"
rm -f -- "${OUT:?}"/zeroos-app-*.deb "${OUT:?}/zeroos-apps.index"
# Every module needs a .pkg, and every .pkg a module: nothing gets left out.
for m in "$MODS"/*.so; do
    [ -f "$HERE/$(basename "$m" .so).pkg" ] || { echo "error: no image/apps/$(basename "$m" .so).pkg for $m" >&2; exit 1; }
done
{
    echo "# ZeroOS apps $VERSION (docs/APPS.md). Read by zeroos-apps on the device."
    echo
    for pkg in "$HERE"/*.pkg; do
        id=$(basename "$pkg" .pkg)
        [ -f "$MODS/$id.so" ] || { echo "error: no module $MODS/$id.so for $pkg" >&2; exit 1; }
        ZEROOS_APP_ABI=$ABI "$HERE/make-app-deb.sh" "$pkg" "$OUT" "$VERSION" "$MODS/$id.so"
    done
} > "$OUT/zeroos-apps.index.tmp"
mv -f "$OUT/zeroos-apps.index.tmp" "$OUT/zeroos-apps.index"
ls -l "$OUT"
