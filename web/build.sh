#!/usr/bin/env bash
# Build the browser simulator: real ZeroShell compiled to WebAssembly, inlined
# into one self-contained HTML page.
#   web/build.sh [out.html]        (needs Emscripten: source emsdk_env.sh first)
#   ZS_APPS=<CardPuter-Apps checkout> web/build.sh [out.html]
# With ZS_APPS, the page is built from the apps repository with this checkout
# as its base, so every app there runs in the browser (the site does this).
# Without it, only the apps this repository still lists are in the page.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/build/web/zeroos-simulator.html}")
if [ -n "${ZS_APPS:-}" ]; then
  SRC=$(cd "$ZS_APPS" && pwd)
  BUILD="$ROOT/build/web-apps"
  emcmake cmake -S "$SRC" -B "$BUILD" -DZS_BACKEND=web -DZEROOS_BASE="$ROOT" >/dev/null
else
  BUILD="$ROOT/build/web"
  emcmake cmake -S "$ROOT/shell" -B "$BUILD" -DZS_BACKEND=web >/dev/null
fi
cmake --build "$BUILD" --target zeroshell -j"$(nproc)"
# The commit goes into the page so bug reports say exactly which build it was.
COMMIT=${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)}
python3 - "$HERE/template.html" "$BUILD/zeroshell.js" "$OUT" "$COMMIT" <<'PY'
import sys
tpl, js, out, commit = sys.argv[1:]
html = open(tpl).read().replace("__ZS_COMMIT__", commit)
code = open(js).read()
assert "</script" not in code
open(out, "w").write(html.replace("/*__ZEROSHELL_JS__*/", code))
print(f"{out} ({len(html) + len(code)} bytes)")
PY
