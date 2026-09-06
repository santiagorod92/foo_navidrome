#!/usr/bin/env bash
# build-mac.sh — cross-compile the foo_navidrome Windows DLL on macOS using
# clang-cl + lld-link + xwin (Microsoft CRT/SDK/ATL) + WTL. No MSVC, no Windows.
#
# Run scripts/win-vm/setup-mac-toolchain.sh once first. Only the x64 target is
# supported here: clang cannot cross-compile ARM64EC (it needs MSVC-only
# intrinsics like __rdtsc); CI (build-windows.yml) builds ARM64EC with real MSVC.
# On Windows-on-ARM, foobar2000 is ARM64EC and loads the x64 DLL via emulation,
# so x64 is enough to runtime-test the component in the VM.
#
# Env overrides (defaults set by setup-mac-toolchain.sh):
#   SDK_ROOT   ~/.local/share/foo_navidrome-sdk/foobar2000
#   PFC_ROOT   ~/.local/share/foo_navidrome-sdk/pfc
#   LIBPPUI_ROOT ~/.local/share/foo_navidrome-sdk/libPPUI
#   XWIN_SDK   ~/.local/share/xwin/sdk
#   WTL_INC    ~/.local/share/wtl/Include
#
# Requires bash 4+ (mapfile). macOS ships 3.2 — re-exec under Homebrew bash.
set -euo pipefail
if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
  exec "$(brew --prefix)/bin/bash" "$0" "$@"
fi

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SDKROOT_DEFAULT="$HOME/.local/share/foo_navidrome-sdk"
SDK_ROOT="${SDK_ROOT:-$SDKROOT_DEFAULT/foobar2000}"
PFC_ROOT="${PFC_ROOT:-$SDKROOT_DEFAULT/pfc}"
LIBPPUI_ROOT="${LIBPPUI_ROOT:-$SDKROOT_DEFAULT/libPPUI}"
XWIN="${XWIN_SDK:-$HOME/.local/share/xwin/sdk}"
WTL="${WTL_INC:-$HOME/.local/share/wtl/Include}"
LLVM="$(brew --prefix llvm)/bin"
LLD="$(brew --prefix lld)/bin"
BUILD="${BUILD:-$REPO/build-win-mac}"
OBJ="$BUILD/obj"; OUT="$BUILD/foo_navidrome.dll"
JOBS="$(sysctl -n hw.ncpu)"
HELPER="$(dirname "$0")/cc1.sh"

fail() { echo "ERROR: $*" >&2; exit 1; }
[ -x "$LLVM/clang-cl" ] || fail "clang-cl missing — run setup-mac-toolchain.sh"
[ -x "$LLD/lld-link" ] || fail "lld-link missing — run setup-mac-toolchain.sh"
[ -f "$SDK_ROOT/helpers/foobar2000+atl.h" ] || fail "SDK not at $SDK_ROOT"
[ -f "$XWIN/crt/include/atlbase.h" ] || fail "xwin SDK (ATL) not at $XWIN"
[ -f "$WTL/atlapp.h" ] || fail "WTL not at $WTL"
[ -f "$SDK_ROOT/shared/shared-x64.lib" ] || fail "shared-x64.lib missing in $SDK_ROOT/shared"

mkdir -p "$OBJ"
PREFIX_H="$BUILD/win_prefix.h"
cat > "$PREFIX_H" <<'EOF'
#pragma once
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif
#include <WinSock2.h>
#include <windows.h>
#include <timeapi.h>
#include <winioctl.h>
EOF

[ -f "$REPO/version.txt" ] && \
  printf '#pragma once\n#define COMPONENT_VERSION "%s"\n' "$(cat "$REPO/version.txt")" > "$REPO/version_generated.h"

SYS_INC=(-imsvc "$WTL" -imsvc "$XWIN/crt/include" -imsvc "$XWIN/sdk/include/um"
         -imsvc "$XWIN/sdk/include/shared" -imsvc "$XWIN/sdk/include/ucrt" -imsvc "$XWIN/sdk/include/winrt")
PROJ_INC=(-I "$REPO/Windows" -I "$REPO" -I "$SDK_ROOT" -I "$SDK_ROOT/.." -I "$PFC_ROOT")
DEFS=(/DWIN32 /D_WINDOWS /D_USRDLL /DUNICODE /D_UNICODE /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /D_SECURE_ATL=1)
# clang-cl reads /Users/... as the /U flag, so sources are passed as /Tp<path>.
# Static CRT (/MT): the local build is x64 but foobar-on-ARM is ARM64EC and only
# bundles the ARM64EC flavour of VCRUNTIME140/MSVCP140. An emulated x64 component
# can't use those, so link the CRT statically to stay self-contained. (CI builds
# native ARM64EC with /MD for releases, where foobar's bundled CRT matches.)
CL_COMMON=(--target=x86_64-pc-windows-msvc /c /std:c++17 /EHsc /MT /GR /w "${DEFS[@]}" /FI"$PREFIX_H" "${SYS_INC[@]}" "${PROJ_INC[@]}")

SRCS=()
while IFS= read -r f; do SRCS+=("$f"); done < <(
  {
    ls "$PFC_ROOT"/*.cpp "$SDK_ROOT/SDK"/*.cpp "$SDK_ROOT/helpers"/*.cpp \
       "$LIBPPUI_ROOT"/*.cpp "$SDK_ROOT/foobar2000_component_client"/*.cpp 2>/dev/null
    # Component sources are parsed from Windows/foo_navidrome.vcxproj — add a
    # new .cpp there and this cross-build picks it up with no edit here.
    bash "$REPO/scripts/component-sources.sh" "$REPO"
  } | grep -vE '/(pfc-fb2k-hooks|nix-objects)\.cpp$'
)

echo "==> compiling ${#SRCS[@]} sources (-j $JOBS) ..."
: > "$BUILD/errors.log"
printf '%s\0' "${CL_COMMON[@]}" > "$BUILD/.clflags"
export CC1_LLVM="$LLVM" CC1_OBJ="$OBJ" CC1_BUILD="$BUILD" \
       CC1_R="$REPO" CC1_SDK="$SDK_ROOT" CC1_PFC="$PFC_ROOT" CC1_PPUI="$LIBPPUI_ROOT"
fail=0
printf '%s\n' "${SRCS[@]}" | xargs -P "$JOBS" -n1 "$HELPER" || fail=1
if [ "$fail" = "1" ]; then
  echo "===== COMPILE FAILED ====="; grep -E "error:|fatal" "$BUILD/errors.log" | sort -u | head -40; exit 1
fi
echo "==> compile OK"

echo "==> linking $OUT ..."
obj_for() {
  local s="$1" key
  key="$(echo "$s" | sed "s#^$REPO/##; s#^$SDK_ROOT/##; s#^$PFC_ROOT/#pfc/#; s#^$LIBPPUI_ROOT/#libPPUI/#; s#/#__#g")"
  echo "$OBJ/${key%.cpp}.obj"
}
OBJS=(); for s in "${SRCS[@]}"; do OBJS+=("$(obj_for "$s")"); done
LIBPATHS=("/libpath:$XWIN/crt/lib/x86_64" "/libpath:$XWIN/sdk/lib/um/x86_64" "/libpath:$XWIN/sdk/lib/ucrt/x86_64")
SYSLIBS=(winhttp.lib crypt32.lib comctl32.lib winmm.lib user32.lib gdi32.lib gdiplus.lib msimg32.lib
         uxtheme.lib ole32.lib oleaut32.lib uuid.lib shell32.lib shlwapi.lib advapi32.lib version.lib kernel32.lib ws2_32.lib)
"$LLD/lld-link" /dll /nologo /machine:x64 "/out:$OUT" \
  "${LIBPATHS[@]}" "$SDK_ROOT/shared/shared-x64.lib" "${OBJS[@]}" "${SYSLIBS[@]}" 2>"$BUILD/link.log" \
  || { echo "===== LINK FAILED ====="; cat "$BUILD/link.log"; exit 1; }
echo "==> built $OUT ($(du -h "$OUT" | cut -f1))"
