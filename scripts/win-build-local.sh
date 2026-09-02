#!/usr/bin/env bash
# win-build-local.sh — cross-compile the Windows x64 foo_navidrome.dll natively
# on Linux with clang-cl + lld-link, and install it into the Wine foobar2000.
#
# foobar2000 runs under Wine on Linux (it's the Windows build), so it loads
# Windows .dll components. The component uses ATL/WTL + WinHTTP, which clang-cl
# compiles fine against the Windows SDK/CRT/ATL provided by `xwin` plus WTL
# headers — no MSVC, no Wine needed for the build itself (Wine only runs foobar).
#
# Toolchain (one-time setup, see ./win-setup-toolchain.sh):
#   - clang-cl, lld-link, llvm-lib      (pacman: llvm clang lld)
#   - xwin SDK/CRT/ATL  -> $XWIN_SDK    (CRT+SDK+ATL, --include-atl)
#   - WTL headers       -> $WTL_INC
#   - foobar2000 SDK as a sibling       (../foobar2000, ../pfc)
#
# Usage:
#   ./win-build-local.sh [--launch] [--clean] [--no-test] [-j N]
#     --launch   relaunch foobar2000 after installing
#     --clean    wipe the object cache and rebuild everything
#     --no-test  skip the unit tests that otherwise gate the build
#     -j N       parallel compile jobs (default: nproc)

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"   # repo root (scripts/ lives one level down)
SDK_ROOT="${SDK_ROOT:-$(cd "$REPO/../foobar2000" 2>/dev/null && pwd || true)}"
PFC_ROOT="${PFC_ROOT:-$(cd "$REPO/../pfc" 2>/dev/null && pwd || true)}"
LIBPPUI_ROOT="${LIBPPUI_ROOT:-$(cd "$REPO/../libPPUI" 2>/dev/null && pwd || true)}"
XWIN_SDK="${XWIN_SDK:-$HOME/.local/share/xwin/sdk}"
WTL_INC="${WTL_INC:-$HOME/.local/share/wtl/Include}"
BUILD="${BUILD:-$REPO/build-win}"
OBJ_DIR="$BUILD/obj"
OUT_DLL="$BUILD/foo_navidrome.dll"
COMPONENT_DIR="$HOME/.foobar2000/profile/user-components-x64/foo_navidrome"
FOOBAR_LAUNCHER="foobar2000"
TARGET="x86_64-pc-windows-msvc"
ARCH_DIR="x86_64"

LAUNCH=0; CLEAN=0; RUN_TESTS=1; JOBS="$(nproc 2>/dev/null || echo 4)"
while [ $# -gt 0 ]; do
  case "$1" in
    --launch) LAUNCH=1; shift ;;
    --clean)  CLEAN=1; shift ;;
    --no-test) RUN_TESTS=0; shift ;;
    -j)       JOBS="${2:?}"; shift 2 ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------
fail() { echo "ERROR: $*" >&2; exit 1; }
for t in clang-cl lld-link; do command -v "$t" >/dev/null || fail "$t not found (pacman -S llvm clang lld)"; done
[ -n "$SDK_ROOT" ] && [ -f "$SDK_ROOT/helpers/foobar2000+atl.h" ] || fail "foobar2000 SDK sibling not found at ../foobar2000 (set SDK_ROOT)"
[ -n "$PFC_ROOT" ] && [ -d "$PFC_ROOT" ] || fail "pfc sibling not found at ../pfc (set PFC_ROOT)"
[ -f "$XWIN_SDK/crt/include/atlbase.h" ] || fail "xwin SDK (with ATL) not found at $XWIN_SDK — run ./win-setup-toolchain.sh"
[ -f "$WTL_INC/atlapp.h" ] || fail "WTL headers not found at $WTL_INC — run ./win-setup-toolchain.sh"
[ -f "$SDK_ROOT/shared/shared-x64.lib" ] || fail "shared-x64.lib not found in $SDK_ROOT/shared"
[ -n "$LIBPPUI_ROOT" ] && [ -d "$LIBPPUI_ROOT" ] || fail "libPPUI sibling not found at ../libPPUI (set LIBPPUI_ROOT)"

[ "$CLEAN" = "1" ] && rm -rf "$BUILD"
mkdir -p "$OBJ_DIR"

# ---------------------------------------------------------------------------
# Unit tests (gate the build — fail fast before the ~minute-long component
# compile). Same suite CI runs on the Windows runner; --no-test to skip.
# ---------------------------------------------------------------------------
if [ "$RUN_TESTS" = "1" ]; then
  echo "==> unit tests ..."
  XWIN_SDK="$XWIN_SDK" "$REPO/scripts/run-unit-tests.sh" win
fi

# main.cpp reads COMPONENT_VERSION from version_generated.h (else falls back to
# "1.0.0"). The macOS Xcode build phase writes this; mirror it here so the local
# DLL reports the version.txt version. Gitignored, shared with the mac build.
if [ -f "$REPO/version.txt" ]; then
  printf '#pragma once\n#define COMPONENT_VERSION "%s"\n' "$(cat "$REPO/version.txt")" > "$REPO/version_generated.h"
  # version_generated.h isn't tracked as a compile dependency, so drop main.obj
  # to force it to pick up a version bump (one cheap TU).
  rm -f "$OBJ_DIR/main.obj"
fi

# Forced-include prefix: the foobar SDK/pfc sources expect a *full* windows.h
# (NOT lean) so COM types (interface, IUnknown, IDataObject) are defined, but
# WinSock2.h must precede windows.h or pfc-lite.h's winsock include clashes with
# the legacy winsock.h. timeapi.h (timeGetTime) and winioctl.h (storage queries
# in file_win32_wrapper.cpp) aren't pulled by windows.h, so add them here. This
# header is force-included ahead of every translation unit.
PREFIX_H="$BUILD/win_prefix.h"
cat > "$PREFIX_H" <<'EOF'
#pragma once
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif
#include <WinSock2.h>   // must precede windows.h (pfc-lite.h requirement)
#include <windows.h>    // full (no WIN32_LEAN_AND_MEAN) -> COM: interface, IUnknown
#include <timeapi.h>    // timeGetTime (pfc/timers.h)
#include <winioctl.h>   // STORAGE_PROPERTY_QUERY etc. (helpers/file_win32_wrapper.cpp)
EOF

# ---------------------------------------------------------------------------
# Include / define / lib flags
# ---------------------------------------------------------------------------
# System headers as -imsvc so their warnings stay quiet. WTL first (atlapp.h
# etc.), then the xwin CRT/ATL, then the Windows SDK families.
SYS_INC=(
  -imsvc "$WTL_INC"
  -imsvc "$XWIN_SDK/crt/include"
  -imsvc "$XWIN_SDK/sdk/include/um"
  -imsvc "$XWIN_SDK/sdk/include/shared"
  -imsvc "$XWIN_SDK/sdk/include/ucrt"
  -imsvc "$XWIN_SDK/sdk/include/winrt"
)
# Project includes. The SDK sources reach pfc via relative paths (../../pfc),
# so the sibling layout does most of the work; these cover the umbrella headers.
PROJ_INC=(
  -I "$REPO/Windows" -I "$REPO"
  -I "$SDK_ROOT" -I "$SDK_ROOT/.." -I "$PFC_ROOT"
)
DEFS=( /DWIN32 /D_WINDOWS /D_USRDLL /DUNICODE /D_UNICODE /DNDEBUG
       /D_CRT_SECURE_NO_WARNINGS /D_SECURE_ATL=1 /DNAVIDROME_DEBUG_LOG=1 )
FORCE=( /FI"$PREFIX_H" )
CL_COMMON=( --target="$TARGET" /c /std:c++17 /EHsc /MD /GR /w
            "${DEFS[@]}" "${FORCE[@]}" "${SYS_INC[@]}" "${PROJ_INC[@]}" )

# Directories the casing resolver may symlink within (system + SDK sources).
CASE_DIRS=(
  "$WTL_INC" "$XWIN_SDK/crt/include"
  "$XWIN_SDK/sdk/include/um" "$XWIN_SDK/sdk/include/shared"
  "$XWIN_SDK/sdk/include/ucrt" "$XWIN_SDK/sdk/include/winrt"
  "$SDK_ROOT/SDK" "$SDK_ROOT/helpers" "$SDK_ROOT/shared" "$PFC_ROOT" "$LIBPPUI_ROOT"
)

# ---------------------------------------------------------------------------
# Source set: SDK static deps (compiled once, cached) + the component sources.
# shared is linked from the prebuilt shared-x64.lib, so it is NOT compiled.
# ---------------------------------------------------------------------------
mapfile -t SRCS < <(
  {
    ls "$PFC_ROOT"/*.cpp \
       "$SDK_ROOT/SDK"/*.cpp \
       "$SDK_ROOT/helpers"/*.cpp \
       "$LIBPPUI_ROOT"/*.cpp \
       "$SDK_ROOT/foobar2000_component_client"/*.cpp 2>/dev/null
    echo "$REPO/main.cpp"
    echo "$REPO/Windows/SubsonicClientWin.cpp"
    echo "$REPO/Windows/NavidromePluginWin.cpp"
    echo "$REPO/Windows/NavidromeInputWin.cpp"
    echo "$REPO/Windows/BrowserWindow.cpp"
    echo "$REPO/Windows/MediaEnrichmentLogic.cpp"
    echo "$REPO/Windows/EsLyricBridge.cpp"
  } |
  # Excluded from the SDK's "FB2K" build configs: pfc-fb2k-hooks.cpp provides the
  # standalone (non-fb2k) crashHook/winFormatSystemErrorMessageHook that would
  # collide with SDK/utility.cpp's real fb2k hooks; nix-objects.cpp is POSIX-only.
  grep -vE '/(pfc-fb2k-hooks|nix-objects)\.cpp$'
)

obj_for() { # map a source path to a unique, flattened object path
  local s="$1" key
  key="$(echo "$s" | sed "s#^$REPO/##; s#^$SDK_ROOT/##; s#^$PFC_ROOT/#pfc/#; s#^$LIBPPUI_ROOT/#libPPUI/#; s#/#__#g")"
  echo "$OBJ_DIR/${key%.cpp}.obj"
}

# ---------------------------------------------------------------------------
# Casing resolver: MSVC SDK / foobar sources include headers under mixed case
# (MMReg.h, StdAfx.h, …) that don't match the on-disk lowercase name on a
# case-sensitive FS. Scan an error log for "'X.h' file not found", find a
# case-insensitive match under CASE_DIRS, and symlink the requested spelling.
fix_casing() { # $1 = error log; returns 0 if it created at least one symlink
  local log="$1" made=0 base hit
  while read -r missing; do
    base="$(basename "$missing")"
    for d in "${CASE_DIRS[@]}"; do
      hit="$(find "$d" -maxdepth 1 -iname "$base" 2>/dev/null | head -1)"
      [ -n "$hit" ] || continue
      if [ ! -e "$(dirname "$hit")/$base" ]; then
        ln -s "$(basename "$hit")" "$(dirname "$hit")/$base" \
          && { echo "  casing: $base -> $(basename "$hit") in $(dirname "$hit")"; made=1; }
      fi
      break
    done
  done < <(grep -ohE "'[^']+\.(h|hpp)' file not found" "$log" 2>/dev/null | sed "s/' file not found//; s/^'//" | sort -u)
  [ "$made" = "1" ]
}

# ---------------------------------------------------------------------------
# Compile (parallel, cached) with up to a few casing-fix retries.
# ---------------------------------------------------------------------------
compile_one() {
  local src="$1" obj; obj="$(obj_for "$src")"
  # cached: skip if obj exists and is newer than its source
  if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then return 0; fi
  clang-cl "${CL_COMMON[@]}" /Fo"$obj" "$src" 2>"$obj.log" || { cat "$obj.log"; return 1; }
  return 0
}
export -f compile_one obj_for
export OBJ_DIR REPO SDK_ROOT PFC_ROOT LIBPPUI_ROOT
# CL_COMMON can't be exported as an array; re-expose as a string for xargs subshells.
printf '%s\0' "${CL_COMMON[@]}" > "$BUILD/.clflags"

run_build_round() {
  : > "$BUILD/round-errors.log"
  local failed=0
  for src in "${SRCS[@]}"; do
    local obj; obj="$(obj_for "$src")"
    if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then continue; fi
    echo "$src"
  done | xargs -P "$JOBS" -I{} bash -c '
    src="{}"; obj="$(obj_for "$src")"
    mapfile -d "" flags < "'"$BUILD"'/.clflags"
    # clang (unlike MSVC) refuses SIMD intrinsics unless the target feature is
    # enabled. pfc/audio_math.cpp hand-rolls SSE/AVX paths behind a runtime CPU
    # dispatch, so enabling the features for this TU is safe.
    extra=()
    case "$(basename "$src")" in audio_math.cpp) extra=(-mavx2 -mfma);; esac
    if clang-cl "${flags[@]}" "${extra[@]}" /Fo"$obj" "$src" 2>"$obj.log"; then exit 0; else cat "$obj.log" >> "'"$BUILD"'/round-errors.log"; exit 1; fi
  ' || failed=1
  return $failed
}

echo "==> compiling ${#SRCS[@]} sources (-j $JOBS) ..."
for round in 1 2 3 4 5; do
  if run_build_round; then echo "==> compile OK"; break; fi
  if grep -q "file not found" "$BUILD/round-errors.log" && fix_casing "$BUILD/round-errors.log"; then
    echo "==> applied casing fixes, retrying (round $round) ..."; continue
  fi
  echo "===== COMPILE FAILED ====="; grep -E "error:|fatal" "$BUILD/round-errors.log" | sort -u | head -40
  exit 1
done

# ---------------------------------------------------------------------------
# Link -> foo_navidrome.dll
# ---------------------------------------------------------------------------
echo "==> linking $OUT_DLL ..."
mapfile -t OBJS < <(for s in "${SRCS[@]}"; do obj_for "$s"; done)
LIBPATHS=(
  "/libpath:$XWIN_SDK/crt/lib/$ARCH_DIR"
  "/libpath:$XWIN_SDK/sdk/lib/um/$ARCH_DIR"
  "/libpath:$XWIN_SDK/sdk/lib/ucrt/$ARCH_DIR"
)
SYSLIBS=( winhttp.lib crypt32.lib comctl32.lib winmm.lib
          user32.lib gdi32.lib gdiplus.lib msimg32.lib uxtheme.lib
          ole32.lib oleaut32.lib uuid.lib shell32.lib shlwapi.lib
          advapi32.lib version.lib kernel32.lib ws2_32.lib )
lld-link /dll /nologo /machine:x64 \
  "/out:$OUT_DLL" \
  "${LIBPATHS[@]}" \
  "$SDK_ROOT/shared/shared-x64.lib" \
  "${OBJS[@]}" \
  "${SYSLIBS[@]}" 2>"$BUILD/link.log" || { echo "===== LINK FAILED ====="; cat "$BUILD/link.log"; exit 1; }
echo "==> built $OUT_DLL ($(du -h "$OUT_DLL" | cut -f1))"

# ---------------------------------------------------------------------------
# Install + package via install-windows.sh (the standalone installer, mirroring
# how mac-dev-build.sh delegates to install-macos.sh).
# ---------------------------------------------------------------------------
BUILT_DLL="$OUT_DLL" "$REPO/scripts/install-windows.sh"

# NAVIDROME_DEBUG_LOG traces (BrowserWindow / SubsonicClientWin / NavidromeInput) land
# here — Wine's Z:\tmp maps to the host /tmp. `make win-logs` tails it colourised.
DBG_LOG="${NAVIDROME_DEBUG_LOG_FILE:-/tmp/foo_navidrome_debug.log}"

if [ "$LAUNCH" = "1" ]; then
  # Fresh log per session so what you see is only this run.
  : > "$DBG_LOG" 2>/dev/null || true
  printf '==== build %s installed %s — foobar2000 relaunch ====\n' \
    "$(cat "$REPO/version.txt" 2>/dev/null || echo '?')" \
    "$(date '+%Y-%m-%d %H:%M:%S')" >> "$DBG_LOG" 2>/dev/null || true
  echo "==> relaunching foobar2000 ..."
  pkill -f 'foobar2000.exe' 2>/dev/null || true
  sleep 1
  nohup "$FOOBAR_LAUNCHER" >/dev/null 2>&1 &
  echo "==> launched. Live debug logs:  make win-logs"
  echo "    (raw file: $DBG_LOG · Preferences › Components / View › Console for the rest)"
else
  echo "==> done. Restart foobar2000 to load it:  $FOOBAR_LAUNCHER"
  echo "    Then watch component traces with:  make win-logs"
fi
