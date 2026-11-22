#!/usr/bin/env bash
# Build the ZeroOS update package: the same files the image installs, as one
# .deb, plus the manifest the on-device updater reads (docs/UPDATE.md).
#
# Usage: image/package.sh [path/to/zeroshell-arm64] [out-dir]
# Env:   ZEROOS_VERSION (default: image/version.sh)
# Needs: dpkg-deb (any Debian or Ubuntu; no root).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BIN=${1:-$ROOT/build/device/zeroshell}
OUT=${2:-$ROOT/build/update}
VERSION=${ZEROOS_VERSION:-$("$HERE/version.sh")}
# The IR code database (docs/IR.md). Leaving it out of an update would
# delete it from the device, so a failed download stops the build.
export ZEROOS_IRDB=${ZEROOS_IRDB:-$ROOT/build/irdb}
if [ ! -f "$ZEROOS_IRDB/index.tsv" ]; then
    python3 -I "$ROOT/tools/irdb/build_irdb.py" --fetch --cache "$ROOT/build/irdb-src" --out "$ZEROOS_IRDB"
fi
exec "$HERE/stage-zeroos/package.sh" "$BIN" "$OUT" "$VERSION"
