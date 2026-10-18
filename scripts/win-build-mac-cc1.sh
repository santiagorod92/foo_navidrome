#!/usr/bin/env bash
set -uo pipefail
if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
  exec "$(brew --prefix)/bin/bash" "$0" "$@"
fi
src="$1"
key="$(echo "$src" | sed "s#^$CC1_R/##; s#^$CC1_SDK/##; s#^$CC1_PFC/#pfc/#; s#^$CC1_PPUI/#libPPUI/#; s#/#__#g")"
obj="$CC1_OBJ/${key%.cpp}.obj"
dep="${obj%.obj}.d"
obj_is_fresh() {
  local d deps
  [ -f "$obj" ] && [ -f "$dep" ] && [ "$obj" -nt "$src" ] || return 1
  mapfile -t deps < <(sed -e '1s/^[^:]*://' -e 's/\\$//' -e 's/\\ /\x01/g' "$dep" |
                      tr ' ' '\n' | grep -v '^$' | tr '\001' ' ')
  for d in "${deps[@]}"; do
    [ -e "$d" ] && [ ! "$d" -nt "$obj" ] || return 1
  done
}
if obj_is_fresh; then exit 0; fi
mapfile -d "" flags < "$CC1_BUILD/.clflags"
extra=(); case "$(basename "$src")" in audio_math.cpp) extra=(-mavx2 -mfma);; esac
if "$CC1_LLVM/clang-cl" "${flags[@]}" "${extra[@]}" /clang:-MMD /clang:-MF"$dep" /Fo"$obj" /Tp"$src" 2>"$obj.log"; then
  exit 0
else
  cat "$obj.log" >> "$CC1_BUILD/errors.log"; exit 1
fi
