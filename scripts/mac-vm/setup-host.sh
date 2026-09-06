#!/usr/bin/env bash
# setup-host.sh — one-time Linux-host setup for the Docker-OSX macOS test loop
# (scripts/mac-vm/). The counterpart to win-vm/setup-mac-toolchain.sh.
#
# Installs the host packages (docker, sshpass), checks /dev/kvm + group
# membership, then pulls the docker-osx image. Idempotent; safe to re-run.
#
# Distro package install is attempted for pacman / apt / dnf; on anything else
# it prints the package list and continues to the checks.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

say() { echo "==> $*"; }
# docker sshpass unzip curl -> required by mac-vm-up.sh; gh -> mac-vm-test --release;
# tigervnc (vncviewer) -> the VNC client mac-vm-up.sh auto-opens; remmina +
# libvncserver -> its VNC plugin (Arch's `remmina` does NOT pull it on its own,
# you get "Install the VNC protocol plugin first" without libvncserver);
# zstd -> snapshot-export/import (back the install up so it's a one-time thing).
PKGS_COMMON="docker sshpass unzip curl gh tigervnc remmina libvncserver zstd"

say "host packages ($PKGS_COMMON)"
if   command -v pacman  >/dev/null; then sudo pacman -S --needed --noconfirm docker sshpass unzip curl github-cli tigervnc remmina libvncserver zstd
elif command -v apt-get >/dev/null; then sudo apt-get update && sudo apt-get install -y docker.io sshpass unzip curl gh tigervnc-viewer remmina zstd
elif command -v dnf     >/dev/null; then sudo dnf install -y docker sshpass unzip curl gh tigervnc remmina zstd
else echo "   unknown package manager — install manually: $PKGS_COMMON"
fi

say "docker daemon"
sudo systemctl enable --now docker 2>/dev/null || echo "   (start the docker daemon yourself)"

if ! docker info >/dev/null 2>&1; then
  say "adding $USER to the 'docker' group (re-login required)"
  sudo usermod -aG docker "$USER" || true
  echo "   log out/in (or: newgrp docker) then re-run this script"
fi

say "KVM"
if [ ! -e /dev/kvm ]; then
  echo "   /dev/kvm missing — enable VT-x/AMD-V in firmware and load kvm_intel / kvm_amd"
  exit 1
fi
if ! { [ -r /dev/kvm ] && [ -w /dev/kvm ]; }; then
  sudo usermod -aG kvm "$USER" || true
  echo "   added $USER to 'kvm' — log out/in, then re-run this script"
  exit 1
fi
echo "   /dev/kvm ok"

say "pulling the docker-osx image"
"$HERE/mac-vm.sh" setup

say "done. Next:"
echo "   scripts/mac-vm/mac-vm.sh install       # GUI macOS install (once, ~30-45 min)"
echo "   scripts/mac-vm/mac-vm.sh run           # boot the installed guest"
echo "   scripts/mac-vm/mac-vm-test.sh --release --launch   # pull CI build -> deploy -> run"
