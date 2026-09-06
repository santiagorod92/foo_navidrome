#!/usr/bin/env bash
# mac-vm-up.sh — `make mac-vm` in one file. Brings a Docker-OSX guest to a state
# where the foo_navidrome component is deployed and foobar2000 is running, doing
# every automatable step itself: host checks, image pull, container create,
# snapshot, foobar2000 install, component deploy, launch.
#
# The ONLY manual step is the first macOS install click-through (Disk Utility +
# Reinstall macOS + Setup Assistant + Remote Login). Apple ships no unattended
# installer and Docker-OSX's current image has no :auto variant, so that part
# can't be scripted. This runs it over VNC, prints the exact clicks, and waits.
# The instant SSH answers it snapshots the disk to <container>:snap — every later
# run then skips straight to deploy+launch in ~1 minute with zero interaction.
#
# Usage:  scripts/mac-vm/mac-vm-up.sh [argsForwarded to mac-vm-test.sh]
#         no args  ->  mac-vm-test.sh --release --launch
#
# Env: CONTAINER, MACOS_VERSION, SSH_PORT, VNC_PORT, MEM, CPUS (see mac-vm.sh);
#      MAC_VM_INSTALL_TIMEOUT (5400s), MAC_VM_BOOT_TIMEOUT (900s).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="$HERE/mac-vm.sh"
CONTAINER="${CONTAINER:-foo_navidrome-macvm}"
SNAP="${CONTAINER}:snap"
BASE_IMAGE="sickcodes/docker-osx:latest"
SSH_PORT="${SSH_PORT:-50922}"
VNC_PORT="${VNC_PORT:-5901}"
SSH_USER="user"; SSH_HOST="localhost"
INSTALL_TIMEOUT="${MAC_VM_INSTALL_TIMEOUT:-5400}"   # 90 min — covers the manual install
BOOT_TIMEOUT="${MAC_VM_BOOT_TIMEOUT:-900}"          # 15 min — a normal boot / snapshot reboot

ssh_opts=(-p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
          -o ConnectTimeout=5 -o PreferredAuthentications=password -o PubkeyAuthentication=no)
sshg() { sshpass -p alpine ssh "${ssh_opts[@]}" "${SSH_USER}@${SSH_HOST}" "$@"; }

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

have_snap() { docker image inspect "$SNAP" >/dev/null 2>&1; }
exists()    { docker inspect "$CONTAINER" >/dev/null 2>&1; }
running()   { [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null)" = true ]; }
is_vnc()    { docker inspect -f '{{range .Config.Env}}{{println .}}{{end}}' "$CONTAINER" 2>/dev/null | grep -q '^EXTRA=.*-vnc'; }

wait_ssh() {  # $1 = timeout seconds, $2 = label
  local deadline=$(( SECONDS + $1 ))
  say "waiting for guest SSH ($2, up to $(( $1 / 60 )) min) — ${SSH_HOST}:${SSH_PORT}"
  while [ "$SECONDS" -lt "$deadline" ]; do
    if sshg true 2>/dev/null; then echo "    SSH up."; return 0; fi
    running || die "container exited while waiting — 'docker logs $CONTAINER' shows why"
    sleep 10
  done
  return 1
}

# ---- 0. host + tools --------------------------------------------------------
say "host preflight"
for t in docker sshpass unzip curl; do
  command -v "$t" >/dev/null || die "missing '$t' — run: make mac-vm-setup"
done
{ [ -e /dev/kvm ] && [ -w /dev/kvm ]; } || die "/dev/kvm not usable by $USER — run: make mac-vm-setup"
docker info >/dev/null 2>&1 || die "docker daemon not reachable for $USER — run: make mac-vm-setup"
docker image inspect "$BASE_IMAGE" >/dev/null 2>&1 || { say "pulling $BASE_IMAGE"; docker pull "$BASE_IMAGE"; }

# ---- 1. get a container running ------------------------------------------
# MAC_VM_FORCE_INSTALL=1 (set by `make mac-vm-reinstall`) ignores every existing
# snapshot / .tar.zst and goes straight to a clean VNC install.
FRESH_INSTALL=0
if [ -n "${MAC_VM_FORCE_INSTALL:-}" ]; then
  exists && { say "MAC_VM_FORCE_INSTALL — removing existing $CONTAINER"; "$MV" rm; }
fi

if [ -z "${MAC_VM_FORCE_INSTALL:-}" ] && exists && ! is_vnc; then
  have_snap || die "existing $CONTAINER is a non-VNC container and there is no snapshot to rebuild from — 'make mac-vm-rm' and retry"
  say "replacing the old non-VNC $CONTAINER with one from $SNAP"
  "$MV" rm
fi

if [ -z "${MAC_VM_FORCE_INSTALL:-}" ] && exists; then
  running && say "container $CONTAINER already running" || { say "starting $CONTAINER"; "$MV" run >/dev/null; }
elif [ -z "${MAC_VM_FORCE_INSTALL:-}" ] && have_snap; then
  say "creating $CONTAINER from snapshot $SNAP — no macOS download, no install"
  IMAGE="$SNAP" "$MV" create
elif [ -z "${MAC_VM_FORCE_INSTALL:-}" ] && [ -f "${CONTAINER}-snap.tar.zst" ]; then
  say "found ${CONTAINER}-snap.tar.zst — importing it instead of reinstalling"
  "$MV" snapshot-import "${CONTAINER}-snap.tar.zst"
  say "creating $CONTAINER from the imported snapshot"
  IMAGE="$SNAP" "$MV" create
else
  FRESH_INSTALL=1
  say "no snapshot — first-time macOS install (this once; then 'make mac-vm-snapshot-export' so it's the last time)"
  echo "    if you have a <container>-snap.tar.zst backup elsewhere: Ctrl-C, put it in \$PWD or run"
  echo "    'make mac-vm-snapshot-import FILE=/path/to/it', then 'make mac-vm' again."
  "$MV" create
  cat <<EOF

  +-- ONE-TIME MANUAL STEP ------------------------------------------------+
  |  VNC viewer:  vncviewer ${SSH_HOST}:${VNC_PORT}   (or remmina; no password)
  |  In the guest:
  |    1. Disk Utility -> erase the largest 'Apple Inc. VirtIO' disk as APFS -> quit
  |    2. Reinstall macOS -> pick that disk -> wait (self-reboots a few times)
  |    3. At the OpenCore picker after each reboot pick 'macOS'
  |       (NOT 'macOS Installer' / 'Recovery')
  |    4. Finish Setup Assistant (skip Apple ID)
  |    5. System Settings -> General -> Sharing -> Remote Login: ON
  |  Then walk away. This script takes over the moment SSH answers, snapshots
  |  the disk, installs foobar2000, deploys the component and launches it.
  +---------------------------------------------------------------------------+
EOF
  # auto-open a VNC client: prefer tigervnc's vncviewer (no plugin faff), else remmina
  if command -v vncviewer >/dev/null 2>&1; then
    (vncviewer "${SSH_HOST}:${VNC_PORT}" >/dev/null 2>&1 &) || true
  elif command -v remmina >/dev/null 2>&1; then
    (remmina -c "vnc://${SSH_HOST}:${VNC_PORT}" >/dev/null 2>&1 &) || true
  else
    echo "    (no VNC client found — 'sudo pacman -S tigervnc' then: vncviewer ${SSH_HOST}:${VNC_PORT})"
  fi
fi

# ---- 2. wait for the guest --------------------------------------------------
if [ "$FRESH_INSTALL" = 1 ]; then
  wait_ssh "$INSTALL_TIMEOUT" "manual macOS install" \
    || die "timed out waiting for the install — rerun 'make mac-vm' to resume where it left off"
else
  wait_ssh "$BOOT_TIMEOUT" "boot" || die "guest did not come up — 'docker logs $CONTAINER'"
fi

# ---- 3. snapshot the fresh install so it is never lost --------------------
if ! have_snap; then
  say "snapshotting the install to $SNAP (one-off, a few minutes — do not interrupt)"
  "$MV" stop
  docker commit "$CONTAINER" "$SNAP" >/dev/null
  docker image inspect "$SNAP" >/dev/null 2>&1 || die "snapshot commit failed — the container is still here, do not 'rm' it"
  echo "    $SNAP saved ($(docker image inspect -f '{{.Size}}' "$SNAP" | numfmt --to=iec 2>/dev/null || echo '?'))"
  "$MV" run >/dev/null
  wait_ssh "$BOOT_TIMEOUT" "reboot after snapshot" || die "guest did not come back after the snapshot"
fi

# ---- 4. provision foobar2000.app -----------------------------------------
if sshg 'test -d /Applications/foobar2000.app' 2>/dev/null; then
  say "foobar2000.app already installed"
else
  say "installing foobar2000.app in the guest"
  DMG_PATH="$(curl -fsSL https://www.foobar2000.org/mac \
    | grep -oE '/downloads/foobar2000-v[0-9][^"]*\.dmg' | head -1 || true)"
  [ -n "$DMG_PATH" ] || die "could not find the foobar2000 macOS .dmg URL on foobar2000.org/mac"
  DMG_URL="https://www.foobar2000.org${DMG_PATH}"
  echo "    $DMG_URL"
  sshg "set -e
    curl -fL --retry 3 -o /tmp/fb.dmg '$DMG_URL'
    mkdir -p /tmp/fbmnt
    hdiutil attach /tmp/fb.dmg -nobrowse -quiet -mountpoint /tmp/fbmnt
    sudo rm -rf /Applications/foobar2000.app
    sudo cp -R /tmp/fbmnt/foobar2000.app /Applications/
    hdiutil detach -quiet /tmp/fbmnt
    sudo xattr -dr com.apple.quarantine /Applications/foobar2000.app 2>/dev/null || true
    rm -f /tmp/fb.dmg"
  echo "    done."
fi

# ---- 5. deploy the component + launch -----------------------------------
say "deploying the component"
if [ "$#" -gt 0 ]; then set -- "$@"; else set -- --release --launch; fi
exec "$HERE/mac-vm-test.sh" "$@"
