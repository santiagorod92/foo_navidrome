#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: win-build-local.sh [--launch] [--clean] [--no-test] [--release-log] [--minor|--major|--patch] [-j N]
  --launch       relaunch foobar2000 after installing
  --clean        wipe the object cache and rebuild everything
  --no-test      skip the unit tests that gate the build
  --release-log  build without NAVIDROME_DEBUG_LOG (release logging)
  --minor/--major/--patch  stamp the last release + 1 (default: git describe)
  -j N           parallel compile jobs (default: nproc)
USAGE
}

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
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

LAUNCH=0; CLEAN=0; RUN_TESTS=1; RELEASE_LOG=0; BUMP=none; JOBS="$(nproc 2>/dev/null || echo 4)"
while [ $# -gt 0 ]; do
  case "$1" in
    --launch) LAUNCH=1; shift ;;
    --clean)  CLEAN=1; shift ;;
    --no-test) RUN_TESTS=0; shift ;;
    --release-log) RELEASE_LOG=1; shift ;;
    --major)  BUMP=major; shift ;;
    --minor)  BUMP=minor; shift ;;
    --patch)  BUMP=patch; shift ;;
    -j)       JOBS="${2:?}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

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

if [ "$RUN_TESTS" = "1" ]; then
  echo "==> unit tests ..."
  XWIN_SDK="$XWIN_SDK" "$REPO/scripts/run-unit-tests.sh" win
fi

if [ "$BUMP" != "none" ]; then
  CURRENT="$("$REPO/scripts/version.sh" --base)"
  IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"
  MAJOR="${MAJOR:-0}"; MINOR="${MINOR:-0}"; PATCH="${PATCH:-0}"
  case "$BUMP" in
    major) MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0 ;;
    minor) MINOR=$((MINOR + 1)); PATCH=0 ;;
    patch) PATCH=$((PATCH + 1)) ;;
  esac
  NEW="${MAJOR}.${MINOR}.${PATCH}"
  echo "==> version: ${CURRENT} -> ${NEW}"
fi

if [ "$BUMP" != "none" ]; then export NAVIDROME_VERSION="$NEW"; fi
BUILD_VERSION="$("$REPO/scripts/version.sh" --header "$REPO/version_generated.h")"
echo "==> component version: $BUILD_VERSION"

PREFIX_H="$BUILD/win_prefix.h"
cat > "$PREFIX_H.new" <<'EOF'
#pragma once
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif
#define _NO_SYS_GUID_OPERATOR_EQ_
#include <WinSock2.h>
#include <windows.h>
#include <timeapi.h>
#include <winioctl.h>
EOF
cmp -s "$PREFIX_H.new" "$PREFIX_H" && rm -f "$PREFIX_H.new" || mv -f "$PREFIX_H.new" "$PREFIX_H"

SYS_INC=(
  -imsvc "$WTL_INC"
  -imsvc "$XWIN_SDK/crt/include"
  -imsvc "$XWIN_SDK/sdk/include/um"
  -imsvc "$XWIN_SDK/sdk/include/shared"
  -imsvc "$XWIN_SDK/sdk/include/ucrt"
  -imsvc "$XWIN_SDK/sdk/include/winrt"
)
PROJ_INC=(
  -I "$REPO/src/platform/win" -I "$REPO"
  -I "$SDK_ROOT" -I "$SDK_ROOT/.." -I "$PFC_ROOT"
)
DEFS=( /DWIN32 /D_WINDOWS /D_USRDLL /DUNICODE /D_UNICODE /DNDEBUG
       /D_CRT_SECURE_NO_WARNINGS /D_SECURE_ATL=1 )
[ "$RELEASE_LOG" = "1" ] || DEFS+=( /DNAVIDROME_DEBUG_LOG=1 )
FORCE=( /FI"$PREFIX_H" )
CL_COMMON=( --target="$TARGET" /c /std:c++20 /utf-8 /EHsc /MD /GR /w
            "${DEFS[@]}" "${FORCE[@]}" "${SYS_INC[@]}" "${PROJ_INC[@]}" )

CASE_DIRS=(
  "$WTL_INC" "$XWIN_SDK/crt/include"
  "$XWIN_SDK/sdk/include/um" "$XWIN_SDK/sdk/include/shared"
  "$XWIN_SDK/sdk/include/ucrt" "$XWIN_SDK/sdk/include/winrt"
  "$SDK_ROOT/SDK" "$SDK_ROOT/helpers" "$SDK_ROOT/shared" "$PFC_ROOT" "$LIBPPUI_ROOT"
)

mapfile -t SRCS < <(
  {
    ls "$PFC_ROOT"/*.cpp \
       "$SDK_ROOT/SDK"/*.cpp \
       "$SDK_ROOT/helpers"/*.cpp \
       "$LIBPPUI_ROOT"/*.cpp \
       "$SDK_ROOT/foobar2000_component_client"/*.cpp 2>/dev/null
    bash "$REPO/scripts/component-sources.sh" "$REPO"
  } |
  grep -vE '/(pfc-fb2k-hooks|nix-objects)\.cpp$'
)

obj_for() {
  local s="$1" key
  key="$(echo "$s" | sed "s#^$REPO/##; s#^$SDK_ROOT/##; s#^$PFC_ROOT/#pfc/#; s#^$LIBPPUI_ROOT/#libPPUI/#; s#/#__#g")"
  echo "$OBJ_DIR/${key%.cpp}.obj"
}

obj_is_fresh() {
  local src="$1" obj="$2" dep="${2%.obj}.d" d deps
  [ -f "$obj" ] && [ -f "$dep" ] && [ "$obj" -nt "$src" ] || return 1
  mapfile -t deps < <(sed -e '1s/^[^:]*://' -e 's/\\$//' -e 's/\\ /\x01/g' "$dep" |
                      tr ' ' '\n' | grep -v '^$' | tr '\001' ' ')
  for d in "${deps[@]}"; do
    [ -e "$d" ] && [ ! "$d" -nt "$obj" ] || return 1
  done
}

FLAGS_STAMP="$OBJ_DIR/.flags"
FLAGS_SUM="$(printf '%s\n' "${CL_COMMON[@]}" | sha1sum)"
if [ "$(cat "$FLAGS_STAMP" 2>/dev/null)" != "$FLAGS_SUM" ]; then
  [ -f "$FLAGS_STAMP" ] && echo "==> compile flags changed, dropping the object cache"
  rm -f "$OBJ_DIR"/*.obj "$OBJ_DIR"/*.d
  echo "$FLAGS_SUM" > "$FLAGS_STAMP"
fi

fix_casing() {
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

export -f obj_for
export OBJ_DIR REPO SDK_ROOT PFC_ROOT LIBPPUI_ROOT
printf '%s\0' "${CL_COMMON[@]}" > "$BUILD/.clflags"

run_build_round() {
  : > "$BUILD/round-errors.log"
  local failed=0
  for src in "${SRCS[@]}"; do
    local obj; obj="$(obj_for "$src")"
    if obj_is_fresh "$src" "$obj"; then continue; fi
    echo "$src"
  done | xargs -P "$JOBS" -I{} bash -c '
    src="{}"; obj="$(obj_for "$src")"
    mapfile -d "" flags < "'"$BUILD"'/.clflags"
    extra=()
    case "$(basename "$src")" in audio_math.cpp) extra=(-mavx2 -mfma);; esac
    if clang-cl "${flags[@]}" "${extra[@]}" /clang:-MMD /clang:-MF"${obj%.obj}.d" /Fo"$obj" "$src" 2>"$obj.log"; then exit 0; else cat "$obj.log" >> "'"$BUILD"'/round-errors.log"; exit 1; fi
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

BUILT_DLL="$OUT_DLL" "$REPO/scripts/install-windows.sh"

DBG_LOG="${NAVIDROME_DEBUG_LOG_FILE:-/tmp/foo_navidrome_debug.log}"

if [ "$LAUNCH" = "1" ]; then
  : > "$DBG_LOG" 2>/dev/null || true
  printf '==== build %s installed %s — foobar2000 relaunch ====\n' \
    "$BUILD_VERSION" \
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
