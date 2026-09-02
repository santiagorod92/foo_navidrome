#!/usr/bin/env bash
# run-unit-tests.sh — build + run the cross-platform logic unit tests.
#
# One source file (tests/MediaEnrichmentLogicTests.cpp) + Windows/MediaEnrichmentLogic.cpp,
# compiled with a per-host toolchain:
#
#   mac  -> native clang++                         (macOS dev / CI)
#   win  -> clang-cl + xwin SDK, run under wine     (Linux cross-compile / CI fast path)
#
# The suite covers SubsonicTypes.h (shared, byte-identical everywhere) and
# MediaEnrichmentLogic.cpp, whose only platform-specific line is MD5
# (#if defined(_WIN32) WinCrypt / #else CommonCrypto).
#
# Called by: Makefile (`make test` / `make mac-test`), scripts/mac-dev-build.sh,
# scripts/win-build-local.sh. Keep those callers pointed here — don't re-inline
# the compile command.
#
# Usage: run-unit-tests.sh [mac|win|auto]   (default: auto — pick by `uname -s`)

set -euo pipefail

MODE="${1:-auto}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC=("$ROOT/tests/MediaEnrichmentLogicTests.cpp" "$ROOT/Windows/MediaEnrichmentLogic.cpp")

if [ "$MODE" = "auto" ]; then
  case "$(uname -s)" in
    Darwin) MODE="mac" ;;
    *)      MODE="win" ;;
  esac
fi

case "$MODE" in
  mac)
    OUT="$ROOT/build-mac/tests"
    mkdir -p "$OUT"
    echo "==> unit tests: clang++ (native macOS)"
    clang++ -std=c++17 -Wall -Wextra -Werror -O1 \
      "${SRC[@]}" -o "$OUT/MediaEnrichmentLogicTests"
    "$OUT/MediaEnrichmentLogicTests"
    ;;
  win)
    XWIN_SDK="${XWIN_SDK:-$HOME/.local/share/xwin/sdk}"
    OUT="$ROOT/build-win/tests"
    command -v clang-cl >/dev/null || {
      echo "ERROR: clang-cl not found (pacman -S llvm clang lld)" >&2; exit 1; }
    [ -f "$XWIN_SDK/crt/include/vcruntime.h" ] || {
      echo "ERROR: xwin SDK not found at $XWIN_SDK — run scripts/win-setup-toolchain.sh" >&2; exit 1; }
    command -v wine >/dev/null || { echo "ERROR: wine not found" >&2; exit 1; }
    mkdir -p "$OUT"
    echo "==> unit tests: clang-cl + wine (Windows target)"
    clang-cl --target=x86_64-pc-windows-msvc -fuse-ld=lld-link /std:c++17 /EHsc /MD /GR /W4 /WX /utf-8 \
      -imsvc "$XWIN_SDK/crt/include" -imsvc "$XWIN_SDK/sdk/include/um" \
      -imsvc "$XWIN_SDK/sdk/include/shared" -imsvc "$XWIN_SDK/sdk/include/ucrt" \
      "${SRC[@]}" \
      /Fe:"$OUT/MediaEnrichmentLogicTests.exe" /Fo:"$OUT/" /link \
      "/libpath:$XWIN_SDK/crt/lib/x86_64" "/libpath:$XWIN_SDK/sdk/lib/um/x86_64" \
      "/libpath:$XWIN_SDK/sdk/lib/ucrt/x86_64" advapi32.lib
    wine "$OUT/MediaEnrichmentLogicTests.exe"
    ;;
  *)
    echo "usage: $0 [mac|win|auto]" >&2
    exit 2
    ;;
esac
