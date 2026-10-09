#!/usr/bin/env bash
set -euo pipefail

REPO="${1:?usage: component-sources.sh <repo-root>}"
PROJ_DIR="$REPO/src/platform/win"
vcxproj="$PROJ_DIR/foo_navidrome.vcxproj"

[ -f "$vcxproj" ] || { echo "component-sources.sh: $vcxproj not found" >&2; exit 1; }

grep -oE 'ClCompile Include="[^"]+"' "$vcxproj" |
  sed 's/.*Include="//; s/"$//' |
  while IFS= read -r inc; do
    inc="${inc//\\//}"
    case "$inc" in
      stdafx.cpp) continue ;;
      /*)         printf '%s\n' "$inc" ;;
      *)
                  printf '%s/%s\n' "$(cd "$PROJ_DIR/$(dirname "$inc")" && pwd)" "$(basename "$inc")" ;;
    esac
  done
