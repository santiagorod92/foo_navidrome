#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
usage: scripts/mac-syntax-check.sh [file...]

Compiles the component's macOS sources with -fsyntax-only against the
foobar2000 SDK, using only the Command Line Tools clang (no Xcode needed).
It catches compile errors in the shared SDK glue (src/core/*.cpp) and the
macOS ObjC++ files (src/platform/mac/*.mm); it does not link or build a
bundle. With no arguments every component source is checked.

SDK lookup is the same as mac-dev-build.sh: sibling ../foobar2000 + ../pfc,
else $FOO_NAVIDROME_SDK (default ~/.local/share/foo_navidrome-sdk).
EOF
}

case "${1:-}" in -h|--help) usage; exit 0 ;; esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -f "$ROOT/../helpers/foobar2000+atl.h" ]; then
  FB="$ROOT/.."; TREE="$ROOT/../.."
else
  TREE="${FOO_NAVIDROME_SDK:-$HOME/.local/share/foo_navidrome-sdk}"
  FB="$TREE/foobar2000"
fi
[ -f "$FB/helpers/foobar2000+atl.h" ] || { echo "ERROR: foobar2000 SDK not found (see mac-dev-build.sh)" >&2; exit 1; }

"$ROOT/scripts/version.sh" --header "$ROOT/version_generated.h" >/dev/null

if [ "$#" -gt 0 ]; then
  FILES=("$@")
else
  FILES=()
  for f in "$ROOT"/src/core/*.cpp "$ROOT"/src/platform/mac/*.mm; do FILES+=("$f"); done
fi

FLAGS=(-x objective-c++ -std=gnu++20 -fobjc-arc -fsyntax-only -DNDEBUG=1 -mmacosx-version-min=12.0
       -I "$ROOT" -I "$FB" -I "$TREE" -include "$ROOT/src/core/stdafx.h"
       -Wall -Wno-documentation-deprecated-sync)

failed=0
for f in "${FILES[@]}"; do
  if out="$(clang++ "${FLAGS[@]}" "$f" 2>&1)"; then
    ours="$(echo "$out" | grep -E "^$ROOT/src/.*warning:" || true)"
    if [ -n "$ours" ]; then echo "== $f"; echo "$ours"; fi
  else
    echo "== $f"; echo "$out" | grep -E -A3 "error:" | head -n 40
    failed=$((failed + 1))
  fi
done
echo "mac syntax check: ${#FILES[@]} file(s), $failed failed"
[ "$failed" -eq 0 ]
