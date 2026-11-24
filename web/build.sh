#!/usr/bin/env bash
# Build the browser simulator: real ZeroShell compiled to WebAssembly, inlined
# into one self-contained HTML page.
#   web/build.sh [out.html]        (needs Emscripten: source emsdk_env.sh first)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/build/web/zeroos-simulator.html}")
emcmake cmake -S "$ROOT/shell" -B "$ROOT/build/web" -DZS_BACKEND=web >/dev/null
cmake --build "$ROOT/build/web" -j"$(nproc)"
# The commit goes into the page so bug reports say exactly which build it was.
COMMIT=${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)}
python3 - "$HERE/template.html" "$ROOT/build/web/zeroshell.js" "$OUT" "$COMMIT" <<'PY'
import sys
tpl, js, out, commit = sys.argv[1:]
html = open(tpl).read().replace("__ZS_COMMIT__", commit)
code = open(js).read()
assert "</script" not in code
open(out, "w").write(html.replace("/*__ZEROSHELL_JS__*/", code))
print(f"{out} ({len(html) + len(code)} bytes)")
PY
