#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
PARENT="$(cd "$REPO/.." && pwd)"
MVM="${MVM:-$PARENT/macos-devbox/mvm}"
[ -x "$MVM" ] || { echo "mvm not found at $MVM — clone macos-devbox next to this repo, or set MVM="; exit 1; }
sshg() { "$MVM" ssh "$@"; }

CLEAN=0; RUN_UNIT=1; TEST=0; DEBUG_LOG=0
for a in "$@"; do
  case "$a" in
    --clean)          CLEAN=1 ;;
    --no-unit-tests)  RUN_UNIT=0 ;;
    --test|--launch)  TEST=1 ;;
    --debug-log)      DEBUG_LOG=1 ;;
    *) echo "unknown arg: $a"; exit 1 ;;
  esac
done

SDK_DIRS=(foobar2000/SDK foobar2000/helpers foobar2000/shared
          foobar2000/foobar2000_component_client foobar2000/helpers-mac pfc libPPUI)
for d in "${SDK_DIRS[@]}"; do
  [ -d "$PARENT/$d" ] || { echo "missing SDK sibling: $PARENT/$d  (see README 'Build layout')"; exit 1; }
done

sshg true 2>/dev/null || { echo "guest not reachable — mvm up --wait (and mvm provision once)"; exit 1; }

if ! sshg 'xcodebuild -version' >/dev/null 2>&1; then
  echo "guest has no working xcodebuild."
  echo "Run once:  $MVM xcode /path/to/Xcode_15.x.xip   (developer.apple.com/download/all)"
  echo "then:      $MVM snapshot xcode"
  exit 1
fi
echo "==> guest xcode: $(sshg 'xcodebuild -version | tr "\n" " "')"

GDIR='~/build/foobar2000/foo_navidrome'

if [ "$CLEAN" = 1 ]; then
  echo "==> --clean: wiping guest ~/build"
  sshg 'rm -rf ~/build'
fi
sshg 'mkdir -p ~/build/foobar2000/foo_navidrome'
sshg "cd $GDIR && find . -maxdepth 1 -mindepth 1 ! -name build -exec rm -rf {} +"

echo "==> pushing SDK siblings (~5 MB) ..."
( cd "$PARENT" && tar cf - "${SDK_DIRS[@]}" ) | sshg 'tar xf - -C ~/build'

echo "==> pushing working tree ..."
tar -C "$REPO" \
    --exclude='./.git' --exclude='./build' --exclude='./build-win' --exclude='./build-mac' \
    --exclude='./_sdk-staging' --exclude='./node_modules' --exclude='*.fb2k-component' \
    -cf - . | sshg "tar xf - -C $GDIR"

if [ "$RUN_UNIT" = 1 ]; then
  echo "==> guest unit tests ..."
  sshg "cd $GDIR && ./scripts/run-unit-tests.sh mac"
fi

VERSION="$("$REPO/scripts/version.sh")"
echo "==> guest xcodebuild Release, version $VERSION ..."
EXTRA_ENV=""
[ "$DEBUG_LOG" = 1 ] && EXTRA_ENV="MAC_EXTRA_CFLAGS=-DNAVIDROME_DEBUG_LOG=1"
sshg "cd $GDIR && $EXTRA_ENV ./scripts/mac-ci-build.sh '$VERSION'"

echo "==> pulling .fb2k-component to $REPO ..."
sshg "cd $GDIR && ls -t foo_navidrome_*.fb2k-component | head -1" >/dev/null || {
  echo "no .fb2k-component produced in the guest"; exit 1; }
GART="$(sshg "cd $GDIR && ls -t foo_navidrome_*.fb2k-component | head -1")"
sshg "cd $GDIR && tar cf - '$GART'" | tar xf - -C "$REPO"
HART="$REPO/$GART"
[ -f "$HART" ] || { echo "pull failed — expected $HART"; exit 1; }
echo "==> built: $HART"

if [ "$TEST" = 1 ]; then
  echo "==> deploying + launching in the guest ..."
  exec "$MVM" deploy "$HART" --launch
fi
echo "==> done. Deploy it with:  make mac-vm-test COMPONENT=\"$HART\""
