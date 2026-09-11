#!/usr/bin/env bash
# mac-vm-build.sh — build the macOS foo_navidrome.component INSIDE the Docker-OSX
# guest (xcodebuild), pull the packaged .fb2k-component back to the host, and
# optionally deploy + launch it in the same guest.
#
# This is the "build on demand without a Mac" path. It needs Xcode installed in
# the guest once — run `./mac-vm.sh provision-xcode` (a manual Xcode_15.x.xip
# drop, then `make mac-vm-snapshot-export` so it is never redone).
#
# What it does, every run:
#   1. push the SDK siblings (../foobar2000/{SDK,helpers,shared,
#      foobar2000_component_client,helpers-mac}, ../pfc, ../libPPUI) into the
#      guest at ~/build/  in the layout the xcworkspace expects
#   2. push this repo's WORKING TREE to ~/build/foobar2000/foo_navidrome/
#      (tracked + untracked, minus .git and build output)
#   3. run scripts/run-unit-tests.sh mac         (skip with --no-unit-tests)
#   4. run scripts/mac-ci-build.sh <version.txt> (xcodebuild Release + package),
#      NO version bump — version.txt is written back to its current value
#   5. tar the resulting foo_navidrome_<v>.fb2k-component back to the repo root
#   6. --test  -> hand it to mac-vm-test.sh <component> --launch
#
# DerivedData is kept in the guest at ~/build/foobar2000/foo_navidrome/build/
# across runs for a faster loop; --clean wipes the whole ~/build tree first.
#
# Requires: ./mac-vm.sh run (guest booted, Remote Login on), sshpass.
#
# Usage:
#   ./mac-vm-build.sh                 # build, pull the .fb2k-component to repo root
#   ./mac-vm-build.sh --test          # ... then deploy + launch it in the guest
#   ./mac-vm-build.sh --clean --test  # wipe guest ~/build first, then build + test
#   ./mac-vm-build.sh --no-unit-tests
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
PARENT="$(cd "$REPO/.." && pwd)"
SSH_PORT="${SSH_PORT:-50922}"
SSH_USER="user"; SSH_HOST="localhost"

ssh_opts=(-p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
          -o ConnectTimeout=5 -o PreferredAuthentications=password -o PubkeyAuthentication=no)
sshg() { sshpass -p alpine ssh "${ssh_opts[@]}" "${SSH_USER}@${SSH_HOST}" "$@"; }

CLEAN=0; RUN_UNIT=1; TEST=0
for a in "$@"; do
  case "$a" in
    --clean)          CLEAN=1 ;;
    --no-unit-tests)  RUN_UNIT=0 ;;
    --test|--launch)  TEST=1 ;;
    *) echo "unknown arg: $a"; exit 1 ;;
  esac
done

command -v sshpass >/dev/null || { echo "need sshpass"; exit 1; }

# --- SDK siblings present on the host? (README build layout) -----------------
SDK_DIRS=(foobar2000/SDK foobar2000/helpers foobar2000/shared
          foobar2000/foobar2000_component_client foobar2000/helpers-mac pfc libPPUI)
for d in "${SDK_DIRS[@]}"; do
  [ -d "$PARENT/$d" ] || { echo "missing SDK sibling: $PARENT/$d  (see README 'Build layout')"; exit 1; }
done

# --- guest reachable + has Xcode --------------------------------------------
echo "==> waiting for guest SSH on ${SSH_HOST}:${SSH_PORT} ..."
for i in $(seq 1 60); do
  sshg true 2>/dev/null && break
  sleep 5
  [ "$i" = 60 ] && { echo "guest SSH not reachable — is ./mac-vm.sh run booted, Remote Login on?"; exit 1; }
done

if ! sshg 'xcodebuild -version' >/dev/null 2>&1; then
  echo "guest has no working xcodebuild."
  echo "Run once:  ./scripts/mac-vm/mac-vm.sh provision-xcode   (drop Xcode_15.x.xip in the repo root first)"
  echo "then:      make mac-vm-snapshot && make mac-vm-snapshot-export"
  exit 1
fi
echo "==> guest xcode: $(sshg 'xcodebuild -version | tr "\n" " "')"

GDIR='~/build/foobar2000/foo_navidrome'   # guest repo dir (literal ~, expanded by the guest shell)

# --- stage the guest tree --------------------------------------------------
if [ "$CLEAN" = 1 ]; then
  echo "==> --clean: wiping guest ~/build"
  sshg 'rm -rf ~/build'
fi
sshg 'mkdir -p ~/build/foobar2000/foo_navidrome'
# keep DerivedData (build/), replace every other file under the repo dir
sshg "cd $GDIR && find . -maxdepth 1 -mindepth 1 ! -name build -exec rm -rf {} +"

echo "==> pushing SDK siblings (~5 MB) ..."
( cd "$PARENT" && tar cf - "${SDK_DIRS[@]}" ) | sshg 'tar xf - -C ~/build'

echo "==> pushing working tree ..."
# tracked + untracked-not-ignored, minus the heavy/irrelevant bits.
tar -C "$REPO" \
    --exclude='./.git' --exclude='./build' --exclude='./build-win' --exclude='./build-mac' \
    --exclude='./_sdk-staging' --exclude='./node_modules' --exclude='*.fb2k-component' \
    -cf - . | sshg "tar xf - -C $GDIR"

# --- build in the guest --------------------------------------------------
if [ "$RUN_UNIT" = 1 ]; then
  echo "==> guest unit tests ..."
  sshg "cd $GDIR && ./scripts/run-unit-tests.sh mac"
fi

echo "==> guest xcodebuild Release (no version bump) ..."
sshg "cd $GDIR && ./scripts/mac-ci-build.sh \"\$(cat version.txt)\""

# --- pull the packaged component back -----------------------------------
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
  exec "$HERE/mac-vm-test.sh" "$HART" --launch
fi
echo "==> done. Deploy it with:  ./scripts/mac-vm/mac-vm-test.sh \"$HART\" --launch"
