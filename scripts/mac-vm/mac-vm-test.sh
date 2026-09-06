#!/usr/bin/env bash
# mac-vm-test.sh — deploy a .fb2k-component into the Docker-OSX guest and
# optionally relaunch foobar2000. The macOS mirror of win-vm-test.sh.
#
# The component is NOT built here (no Xcode in the container). Source order:
#   1. $1 if given                          (path to a .fb2k-component)
#   2. --release[=TAG]  -> gh release download (default: latest)
#   3. newest foo_navidrome*.fb2k-component in the repo root
#
# Requires: ./mac-vm.sh run  (guest booted, Remote Login enabled), sshpass,
# unzip, and gh for --release.
#
# Usage:
#   ./mac-vm-test.sh [component.fb2k-component] [--launch]
#   ./mac-vm-test.sh --release --launch
#   ./mac-vm-test.sh --release=v1.12.0
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SSH_PORT="${SSH_PORT:-50922}"
SSH_USER="user"; SSH_HOST="localhost"
GH_REPO="${GH_REPO:-santiagorod92/foo_navidrome}"

ssh_opts=(-p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
          -o ConnectTimeout=5 -o PreferredAuthentications=password -o PubkeyAuthentication=no)
scp_opts=(-P "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
          -o PreferredAuthentications=password -o PubkeyAuthentication=no)
sshg() { sshpass -p alpine ssh "${ssh_opts[@]}" "${SSH_USER}@${SSH_HOST}" "$@"; }

LAUNCH=0; SRC=""; RELEASE=""
for a in "$@"; do
  case "$a" in
    --launch)          LAUNCH=1 ;;
    --release)         RELEASE="latest" ;;
    --release=*)       RELEASE="${a#--release=}" ;;
    *.fb2k-component)  SRC="$a" ;;
    *) echo "unknown arg: $a"; exit 1 ;;
  esac
done

command -v sshpass >/dev/null || { echo "need sshpass"; exit 1; }
command -v unzip   >/dev/null || { echo "need unzip"; exit 1; }

tmp=""; work=""
cleanup() { rm -rf "${tmp:-/nonexistent}" "${work:-/nonexistent}"; }
trap cleanup EXIT

if [ -n "$RELEASE" ]; then
  command -v gh >/dev/null || { echo "need gh for --release"; exit 1; }
  tmp="$(mktemp -d)"
  if [ "$RELEASE" = latest ]; then
    gh release download --repo "$GH_REPO" --pattern '*.fb2k-component' --dir "$tmp"
  else
    gh release download "$RELEASE" --repo "$GH_REPO" --pattern '*.fb2k-component' --dir "$tmp"
  fi
  SRC="$(ls -t "$tmp"/*.fb2k-component 2>/dev/null | head -1 || true)"
fi

[ -n "$SRC" ] || SRC="$(ls -t "$REPO"/foo_navidrome*.fb2k-component 2>/dev/null | head -1 || true)"
[ -n "$SRC" ] && [ -f "$SRC" ] || { echo "no .fb2k-component found — pass a path or --release"; exit 1; }
echo "==> component: $SRC"

# foobar2000 v2.6+ packages the mac bundle under mac/ inside the zip.
work="$(mktemp -d)"
unzip -oq "$SRC" -d "$work"
BUNDLE="$work/mac/foo_navidrome.component"
[ -d "$BUNDLE" ] || BUNDLE="$(find "$work" -name 'foo_navidrome.component' -type d | head -1 || true)"
[ -d "$BUNDLE" ] || { echo "no foo_navidrome.component inside $SRC"; exit 1; }

# Docker-OSX is an emulated Intel Mac — the bundle needs an x86_64 slice or
# foobar silently won't load it.
BIN="$BUNDLE/Contents/MacOS/foo_navidrome"
if command -v lipo >/dev/null 2>&1 && [ -f "$BIN" ]; then
  lipo -archs "$BIN" | grep -qw x86_64 || echo "WARNING: no x86_64 slice in the bundle — it will not load in the guest"
fi

echo "==> waiting for guest SSH on ${SSH_HOST}:${SSH_PORT} ..."
for i in $(seq 1 60); do
  sshg true 2>/dev/null && break
  sleep 5
  [ "$i" = 60 ] && { echo "guest SSH not reachable — is ./mac-vm.sh run booted, macOS installed, Remote Login on?"; exit 1; }
done

DEST='Library/foobar2000-v2/user-components/foo_navidrome'
echo "==> deploying ..."
sshg "killall foobar2000 2>/dev/null; rm -rf ~/$DEST; mkdir -p ~/$DEST"
# tar over ssh keeps the bundle tree + exec bits intact.
( cd "$(dirname "$BUNDLE")" && tar cf - foo_navidrome.component ) | sshg "tar xf - -C ~/$DEST"
# macOS kills a bundle whose signature no longer matches its bytes — ad-hoc
# re-sign in the guest, same as install-macos.sh does on a real Mac.
sshg "codesign --sign - --force --deep ~/$DEST/foo_navidrome.component"

if [ "$LAUNCH" = 1 ]; then
  echo "==> launching foobar2000 ..."
  sshg 'open -a foobar2000 2>/dev/null || open /Applications/foobar2000.app' || \
    echo "could not launch — is foobar2000.app in /Applications?"
fi
echo "==> done. In the guest: File > Open Navidrome Browser; Preferences > Media Library > Navidrome."
