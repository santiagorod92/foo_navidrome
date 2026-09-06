#!/usr/bin/env bash
# mac-vm.sh — run a macOS VM in Docker (sickcodes/docker-osx) on a Linux host
# with /dev/kvm, to runtime-test the foo_navidrome macOS component without owning
# a Mac. The macOS mirror of scripts/win-vm/.
#
# It does NOT build the component (no Xcode in the container). It loads a
# .fb2k-component built by CI (the macos-14 runner) or on a real Mac, then
# launches foobar2000 so you can drive the Navidrome browser UI.
#
#   Guest    : x86_64 macOS — QEMU emulates an Intel Mac, there is no Apple
#              Silicon guest, so the bundle must carry an x86_64 slice. The CI
#              build is universal (arm64 + x86_64), which satisfies this;
#              mac-vm-test.sh warns if the slice is missing.
#   SSH      : host localhost:$SSH_PORT -> guest 10022   (user / alpine)
#   GUI      : X11 (XWayland is fine) unless MAC_VM_VNC=1 -> headless + VNC :1.
#              The GUI transport is fixed at 'install' time (docker create args);
#              switching it later means 'rm' + 'install' again. In X11 mode the
#              host `xhost` grant rotates on re-login, so 'run' re-asserts
#              `xhost +local:root` before starting — without it QEMU can't open
#              DISPLAY, exits 0 a few minutes in, and looks like a boot hang.
#   Disk     : lives in the named container's writable layer. 'rm' destroys it;
#              'snapshot' commits it to $CONTAINER:snap so it can be reused as the
#              base IMAGE for a fresh 'install' (skips the macOS re-download).
#
# Apple's macOS EULA permits virtualization only on Apple-branded hardware. This
# harness is for local development testing; know your obligations before using it.
#
# Subcommands:
#   ./mac-vm.sh setup       # pull the image, preflight /dev/kvm + docker
#   ./mac-vm.sh install     # first boot: GUI macOS install (manual, ~30-45 min).
#                           #   Run this DIRECTLY in a terminal, not via make —
#                           #   and leave it open until macOS is fully installed.
#   ./mac-vm.sh create      # like 'install' but DETACHED + always VNC — no TTY,
#                           #   no X11. Used by mac-vm-up.sh so `make mac-vm`
#                           #   needs no terminal babysitting. Honors IMAGE=.
#   ./mac-vm.sh run         # boot the installed VM detached (SSH + GUI)
#   ./mac-vm.sh ssh [cmd]   # ssh into the guest (user / alpine)
#   ./mac-vm.sh logs        # follow the container's QEMU/boot log
#   ./mac-vm.sh vnc         # print the VNC address (MAC_VM_VNC=1 mode)
#   ./mac-vm.sh stop        # stop the container
#   ./mac-vm.sh rm          # stop + delete the container (loses the macOS disk)
#   ./mac-vm.sh wipe        # rm the container AND the :snap image (clean slate)
#   ./mac-vm.sh snapshot    # docker commit the current disk to $CONTAINER:snap
#   ./mac-vm.sh snapshot-export [file]   # save :snap to a portable .tar.zst
#   ./mac-vm.sh snapshot-import [file]   # load such a .tar.zst back as :snap
#
# Env overrides: MACOS_VERSION (sonoma), CONTAINER (foo_navidrome-macvm),
#   IMAGE (sickcodes/docker-osx:latest — set to $CONTAINER:snap to reuse a
#   snapshot as the base and skip the macOS download), SSH_PORT (50922),
#   VNC_PORT (5901), MEM (8, gigabytes), CPUS (4), NOPICKER (false — set true
#   only once the OpenCore installer/recovery entries are gone, for unattended
#   'run'), NETWORKING (vmxnet3 — try e1000-82545em if the install network-stalls),
#   MAC_VM_VNC (unset = X11).
set -euo pipefail

# Docker Hub only ships one tag (:latest); the macOS release is chosen at run
# time via SHORTNAME (sonoma|ventura|monterey|big-sur|catalina|sequoia|...).
MACOS_VERSION="${MACOS_VERSION:-sonoma}"
IMAGE="${IMAGE:-sickcodes/docker-osx:latest}"
CONTAINER="${CONTAINER:-foo_navidrome-macvm}"
SSH_PORT="${SSH_PORT:-50922}"
VNC_PORT="${VNC_PORT:-5901}"
MEM="${MEM:-8}"
CPUS="${CPUS:-4}"
NOPICKER="${NOPICKER:-false}"
# docker-osx default is vmxnet3; e1000-82545em is slower but far more reliable
# under Sonoma+QEMU — switch if the install hangs with "Slirp: Failed to send
# packet" or stalls at "Setting up your Mac".
NETWORKING="${NETWORKING:-vmxnet3}"
SSH_HOST="localhost"
SSH_USER="user"          # docker-osx default; password 'alpine'

ssh_opts=(-p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
          -o ConnectTimeout=5 -o PreferredAuthentications=password -o PubkeyAuthentication=no)

preflight() {
  command -v docker >/dev/null || { echo "missing: docker"; exit 1; }
  [ -e /dev/kvm ] || { echo "no /dev/kvm — enable virtualization / load the kvm module"; exit 1; }
  { [ -r /dev/kvm ] && [ -w /dev/kvm ]; } || { echo "/dev/kvm not accessible by $USER — add yourself to the 'kvm' group"; exit 1; }
  docker info >/dev/null 2>&1 || { echo "docker daemon not reachable for $USER"; exit 1; }
}

# Let the container (running as its own uid) open the host X server. The grant is
# tied to the host login session and is lost on re-login, so this must run again
# before every 'run', not just once at 'install'. No-op / harmless under VNC.
#
# X11 passthrough is best-effort. It needs `xhost` present AND a host GL stack the
# containerised QEMU GTK can use — on a Wayland session with an NVIDIA
# proprietary driver (no `nvidia-drm` in the container, `glx: failed to create
# dri3 screen`) the window comes up black or not at all even when auth is fine.
# On such hosts use MAC_VM_VNC=1 and a VNC client (remmina, vncviewer).
grant_x() {
  if ! command -v xhost >/dev/null 2>&1; then
    echo "WARNING: 'xhost' not installed — the container cannot be authorised on the" >&2
    echo "         X server. Install it (Arch: xorg-xhost) or use MAC_VM_VNC=1." >&2
    return 0
  fi
  xhost +local:root >/dev/null 2>&1 || true
}

# True if $CONTAINER was created in VNC mode (EXTRA carries -vnc).
container_is_vnc() {
  docker inspect -f '{{range .Config.Env}}{{println .}}{{end}}' "$CONTAINER" 2>/dev/null \
    | grep -q '^EXTRA=.*-vnc'
}

# GUI transport args for 'install' (docker create time). Published ports and
# -e DISPLAY can't be changed on a stopped container afterwards.
gui_args() {
  if [ -n "${MAC_VM_VNC:-}" ]; then
    printf '%s\0' -p "${VNC_PORT}:5901" -e "EXTRA=-display none -vnc 0.0.0.0:1,to=1"
  else
    grant_x
    printf '%s\0' -e "DISPLAY=${DISPLAY:-:0}" -v /tmp/.X11-unix:/tmp/.X11-unix
  fi
}

exists() { docker inspect "$CONTAINER" >/dev/null 2>&1; }
running() { [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null)" = "true" ]; }

case "${1:-}" in
  setup)
    preflight
    echo "==> docker pull $IMAGE  (image is a few GB)"
    docker pull "$IMAGE"
    command -v sshpass >/dev/null || echo "note: install 'sshpass' before using ssh / mac-vm-test.sh"
    echo "==> ok. Next: ./mac-vm.sh install"
    ;;

  install)
    preflight
    exists && { echo "container $CONTAINER already exists — use 'run', or 'rm' it first"; exit 1; }
    [ -t 0 ] || echo "WARNING: stdin is not a TTY. Run this directly in a terminal, NOT via" \
                     "'make mac-vm-install' — QEMU's -monitor stdio needs the TTY or QEMU" \
                     "exits 0 a few minutes in, which looks exactly like a boot hang."
    mapfile -d '' GUI < <(gui_args)
    echo "==> creating $CONTAINER and booting the macOS installer."
    echo "    base image: $IMAGE"
    [ "$IMAGE" = "sickcodes/docker-osx:latest" ] || \
      echo "    (reusing a snapshot — the macOS download is skipped)"
    echo "    Leave THIS terminal open until macOS is fully installed."
    echo "    In the guest:"
    echo "      1. Disk Utility -> erase the largest 'Apple Inc. VirtIO' disk as APFS -> quit"
    echo "      2. Reinstall macOS -> pick that disk -> wait (~30-45 min, reboots itself)"
    echo "      3. At the OpenCore picker after each reboot, choose 'macOS' (the plain"
    echo "         internal disk) — NOT 'macOS Installer' / 'Recovery'"
    echo "      4. Finish Setup Assistant (skip Apple ID)"
    echo "      5. System Settings -> General -> Sharing -> enable Remote Login"
    echo "         (or in Terminal: sudo systemsetup -setremotelogin on)"
    echo "      6. Drag foobar2000.app into /Applications (download the Intel build"
    echo "         from foobar2000.org)"
    [ -n "${MAC_VM_VNC:-}" ] && echo "    VNC: ${SSH_HOST}:${VNC_PORT}" || echo "    GUI opens in an X11 window."
    docker run -it --name "$CONTAINER" \
      --device /dev/kvm \
      -p "${SSH_PORT}:10022" \
      -e "SHORTNAME=$MACOS_VERSION" \
      -e "RAM=$MEM" -e "SMP=$CPUS" -e "CORES=$CPUS" \
      -e "NOPICKER=$NOPICKER" \
      -e "NETWORKING=$NETWORKING" \
      -e "GENERATE_UNIQUE=true" \
      "${GUI[@]}" \
      "$IMAGE"
    ;;

  create)
    preflight
    exists && { echo "container $CONTAINER already exists — 'run' it, or 'rm' first"; exit 1; }
    MAC_VM_VNC=1              # detached => no X11; VNC is the only sane transport
    mapfile -d '' GUI < <(gui_args)
    echo "==> creating $CONTAINER detached (base image: $IMAGE)"
    [ "$IMAGE" = "sickcodes/docker-osx:latest" ] || echo "    (reusing a snapshot — macOS download skipped)"
    docker run -d --name "$CONTAINER" \
      --device /dev/kvm \
      -p "${SSH_PORT}:10022" \
      -e "SHORTNAME=$MACOS_VERSION" \
      -e "RAM=$MEM" -e "SMP=$CPUS" -e "CORES=$CPUS" \
      -e "NOPICKER=$NOPICKER" \
      -e "NETWORKING=$NETWORKING" \
      -e "GENERATE_UNIQUE=true" \
      "${GUI[@]}" \
      "$IMAGE" >/dev/null
    echo "==> up. VNC ${SSH_HOST}:${VNC_PORT} (no password) · SSH ${SSH_HOST}:${SSH_PORT} once macOS is in"
    ;;

  run)
    preflight
    exists || { echo "no container $CONTAINER — run 'install' first"; exit 1; }
    running && { echo "already running (SSH ${SSH_HOST}:${SSH_PORT})"; exit 0; }
    if container_is_vnc; then
      echo "==> starting $CONTAINER (SSH ${SSH_HOST}:${SSH_PORT}, VNC ${SSH_HOST}:${VNC_PORT})"
    else
      grant_x   # the grant is lost on host re-login; re-assert it or QEMU can't open DISPLAY
      echo "==> starting $CONTAINER (SSH ${SSH_HOST}:${SSH_PORT}, GUI on X11 ${DISPLAY:-:0})"
    fi
    docker start "$CONTAINER" >/dev/null
    echo "==> booting. Follow it with: ./mac-vm.sh logs"
    echo "    At the OpenCore picker choose 'macOS' — first real boot rebuilds"
    echo "    kext caches (10-20 min, progress bar barely moves)."
    ;;

  ssh)
    shift
    command -v sshpass >/dev/null || { echo "need sshpass"; exit 1; }
    exec sshpass -p alpine ssh "${ssh_opts[@]}" "${SSH_USER}@${SSH_HOST}" "$@"
    ;;

  logs)  exec docker logs -f "$CONTAINER" ;;
  vnc)
    echo "VNC ${SSH_HOST}:${VNC_PORT} (no password)"
    if   command -v vncviewer >/dev/null 2>&1; then echo "  -> vncviewer"; exec vncviewer "127.0.0.1:${VNC_PORT}"
    elif command -v remmina   >/dev/null 2>&1; then echo "  -> remmina (needs the 'libvncserver' pkg)"; exec remmina -c "vnc://127.0.0.1:${VNC_PORT}"
    else echo "  no VNC client — sudo pacman -S tigervnc  then: vncviewer 127.0.0.1:${VNC_PORT}"; exit 1
    fi
    ;;
  stop)  docker stop "$CONTAINER" >/dev/null 2>&1 || true ;;
  rm)    docker rm -f -v "$CONTAINER" >/dev/null 2>&1 || true; echo "removed $CONTAINER (macOS disk gone)" ;;

  wipe)  # container + writable-layer disk + the :snap image, in one shot
    docker rm -f -v "$CONTAINER" >/dev/null 2>&1 || true
    docker image rm -f "${CONTAINER}:snap" >/dev/null 2>&1 || true
    echo "wiped $CONTAINER and ${CONTAINER}:snap"
    [ -f "${CONTAINER}-snap.tar.zst" ] && echo "(kept ${CONTAINER}-snap.tar.zst backup — delete it by hand for a truly clean slate)" || true
    ;;
  snapshot)
    exists || { echo "no container $CONTAINER"; exit 1; }
    echo "==> committing ${CONTAINER}:snap (tens of GB — can take minutes, do not interrupt)"
    docker commit "$CONTAINER" "${CONTAINER}:snap" >/dev/null
    docker image inspect "${CONTAINER}:snap" >/dev/null 2>&1 \
      || { echo "ERROR: commit did not produce ${CONTAINER}:snap — do NOT 'rm' the container"; exit 1; }
    echo "committed ${CONTAINER}:snap  ($(docker image inspect -f '{{.Size}}' "${CONTAINER}:snap" | numfmt --to=iec 2>/dev/null || echo '?') on disk)"
    echo "back it up off this machine so you never reinstall again:  $0 snapshot-export"
    ;;

  snapshot-export)
    out="${2:-${CONTAINER}-snap.tar.zst}"
    docker image inspect "${CONTAINER}:snap" >/dev/null 2>&1 || { echo "no ${CONTAINER}:snap — run '$0 snapshot' first"; exit 1; }
    command -v zstd >/dev/null 2>&1 || { echo "need 'zstd' (pacman -S zstd)"; exit 1; }
    echo "==> exporting ${CONTAINER}:snap -> $out  (tens of GB, a few minutes)"
    docker save "${CONTAINER}:snap" | zstd -T0 -3 -o "$out"
    echo "wrote $out ($(du -h "$out" | cut -f1)). Restore anywhere with:  $0 snapshot-import $out"
    ;;

  snapshot-import)
    in="${2:-${CONTAINER}-snap.tar.zst}"
    [ -f "$in" ] || { echo "no such file: $in"; exit 1; }
    command -v zstd >/dev/null 2>&1 || { echo "need 'zstd' (pacman -S zstd)"; exit 1; }
    echo "==> importing $in -> ${CONTAINER}:snap"
    zstd -dc "$in" | docker load
    docker image inspect "${CONTAINER}:snap" >/dev/null 2>&1 \
      || { echo "ERROR: load did not produce ${CONTAINER}:snap"; exit 1; }
    echo "ok — 'make mac-vm' will now boot from it, no install."
    ;;

  *) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac
