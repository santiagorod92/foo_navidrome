#!/usr/bin/env bash
set -euo pipefail

SDKDIR="$HOME/.local/share/foo_navidrome-sdk"
XWIN_SDK="$HOME/.local/share/xwin/sdk"
WTL_DIR="$HOME/.local/share/wtl"
WTL_URL="https://downloads.sourceforge.net/project/wtl/WTL%2010/WTL%2010.0.10320%20Release/WTL10_10320_Release.zip"
SDK_REPO="https://github.com/reupen/foobar2000-sdk-unmodified"

say() { echo "==> $*"; }
command -v brew >/dev/null || { echo "Homebrew required: https://brew.sh"; exit 1; }

say "Homebrew packages"
brew install llvm lld bash xwin colima docker docker-compose
mkdir -p "$HOME/.docker/cli-plugins"
ln -sfn "$(brew --prefix)/opt/docker-compose/bin/docker-compose" "$HOME/.docker/cli-plugins/docker-compose"

if [ ! -f "$XWIN_SDK/crt/include/atlbase.h" ]; then
  say "xwin splat (Microsoft CRT/SDK/ATL, x86_64 + aarch64) — ~1GB"
  xwin --accept-license --include-atl --arch x86_64,aarch64 splat --output "$XWIN_SDK"
fi

if [ ! -f "$WTL_DIR/Include/atlapp.h" ]; then
  say "WTL headers"
  mkdir -p "$WTL_DIR"; tmp="$(mktemp -d)"
  curl -sL -o "$tmp/wtl.zip" "$WTL_URL"
  unzip -oq "$tmp/wtl.zip" -d "$WTL_DIR"; rm -rf "$tmp"
fi

if [ ! -f "$SDKDIR/foobar2000/helpers/foobar2000+atl.h" ]; then
  say "foobar2000 SDK (reupen mirror)"
  tmp="$(mktemp -d)"
  git clone --depth 1 "$SDK_REPO" "$tmp/sdk"
  mkdir -p "$SDKDIR"
  mv "$tmp/sdk/foobar2000" "$SDKDIR/foobar2000"
  mv "$tmp/sdk/pfc"        "$SDKDIR/pfc"
  mv "$tmp/sdk/libPPUI"    "$SDKDIR/libPPUI"
  rm -rf "$tmp"
fi

say "Done. Next:"
echo "   make win-build-mac      # cross-compile build-win-mac/foo_navidrome.dll"
echo "   make win11-arm-up       # Windows 11 ARM test VM in a container, then make win11-arm-open"
