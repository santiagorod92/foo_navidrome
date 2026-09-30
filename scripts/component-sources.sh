#!/usr/bin/env bash
# Single source of truth for the component's own translation units.
#
# The list is parsed straight out of src/platform/win/foo_navidrome.vcxproj (the
# ClCompile ItemGroup), so adding a .cpp there is now the *only* edit needed —
# both clang-cl builds pick it up automatically:
#   - scripts/win-build-local.sh   (local Linux -> Wine loop)
#   - scripts/win-vm/build-mac.sh  (macOS cross-build for the ARM QEMU guest)
# The MSBuild/CI path reads the vcxproj directly, so all three stay in lockstep.
#
# stdafx.cpp is skipped on purpose: it exists only to build the MSVC precompiled
# header, which neither clang-cl build uses (they include the headers directly).
#
# Prints one absolute path per line.
set -euo pipefail

REPO="${1:?usage: component-sources.sh <repo-root>}"
PROJ_DIR="$REPO/src/platform/win"
vcxproj="$PROJ_DIR/foo_navidrome.vcxproj"

[ -f "$vcxproj" ] || { echo "component-sources.sh: $vcxproj not found" >&2; exit 1; }

grep -oE 'ClCompile Include="[^"]+"' "$vcxproj" |
  sed 's/.*Include="//; s/"$//' |
  while IFS= read -r inc; do
    inc="${inc//\\//}"                       # vcxproj uses backslashes
    case "$inc" in
      stdafx.cpp) continue ;;                # MSVC-PCH bootstrap, not for clang-cl
      /*)         printf '%s\n' "$inc" ;;     # already absolute
      *)          # relative to the vcxproj (e.g. ../../core/x.cpp); cd+pwd
                  # normalises the ../ (portable — macOS realpath has no -m)
                  printf '%s/%s\n' "$(cd "$PROJ_DIR/$(dirname "$inc")" && pwd)" "$(basename "$inc")" ;;
    esac
  done
