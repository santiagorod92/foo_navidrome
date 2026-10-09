#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: win-vm.sh <install|run|ssh|stop>
  install   first-time unattended install (attaches ISOs)
  run       boot the installed VM
  ssh       ssh into the guest (tester/tester)
  stop      power off
Env overrides: WIN_ISO, VIRTIO_ISO, VMDIR.
USAGE
}

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
VMDIR="${VMDIR:-$HOME/.local/share/foo_navidrome-winvm}"
DISK="$VMDIR/disk.qcow2"
VARS="$VMDIR/efi_vars.fd"
UNATTEND_ISO="$VMDIR/unattend.iso"
WIN_ISO="${WIN_ISO:-$VMDIR/win11-arm64.iso}"
VIRTIO_ISO="${VIRTIO_ISO:-$VMDIR/virtio-win.iso}"
SSH_PORT="${SSH_PORT:-2222}"
MEM="${MEM:-6144}"; CPUS="${CPUS:-4}"; DISK_GB="${DISK_GB:-48}"

QEMU="$(command -v qemu-system-aarch64)"
EDK2="$(brew --prefix qemu)/share/qemu/edk2-aarch64-code.fd"

mkdir -p "$VMDIR"

prep() {
  [ -f "$DISK" ] || qemu-img create -f qcow2 "$DISK" "${DISK_GB}G"
  if [ ! -f "$VARS" ]; then dd if=/dev/zero of="$VARS" bs=1m count=64 2>/dev/null; fi
  if [ ! -f "$UNATTEND_ISO" ]; then
    [ -f "$VIRTIO_ISO" ] || { echo "missing $VIRTIO_ISO"; exit 1; }
    local stage="$VMDIR/_unattend"; rm -rf "$stage"; mkdir -p "$stage/\$WinPEDriver\$/netkvm"
    cp "$HERE/autounattend.xml" "$stage/autounattend.xml"
    cat > "$stage/startup.nsh" <<'NSH'
@echo -off
for %f in fs0 fs1 fs2 fs3 fs4 fs5 fs6 fs7
  if exist %f:\efi\boot\bootaa64.efi then
    echo Booting Windows from %f:
    %f:\efi\boot\bootaa64.efi
  endif
endfor
echo No bootaa64.efi found on any volume.
NSH
    local viso; viso="$(hdiutil attach -nobrowse -readonly "$VIRTIO_ISO" | awk 'END{print $NF}')"
    cp "$viso"/NetKVM/w11/ARM64/* "$stage/\$WinPEDriver\$/netkvm/" 2>/dev/null || \
      cp "$viso"/NetKVM/w11/arm64/* "$stage/\$WinPEDriver\$/netkvm/"
    hdiutil detach "$viso" >/dev/null
    mkisofs -quiet -J -r -V UNATTEND -o "$UNATTEND_ISO" "$stage"
    rm -rf "$stage"
  fi
}

common_args() {
  echo "-machine virt,highmem=on -accel hvf -cpu host -smp $CPUS -m $MEM \
    -drive if=pflash,format=raw,readonly=on,file=$EDK2 \
    -drive if=pflash,format=raw,file=$VARS \
    -device nvme,drive=hd,serial=navi -drive if=none,id=hd,file=$DISK,format=qcow2 \
    -netdev user,id=net0,hostfwd=tcp::$SSH_PORT-:22 -device virtio-net-pci,netdev=net0 \
    -device qemu-xhci -device usb-kbd -device usb-tablet \
    -audiodev coreaudio,id=snd0 -device intel-hda -device hda-output,audiodev=snd0 \
    -device ramfb -display ${VM_DISPLAY:-none} -serial file:$VMDIR/serial.log \
    -rtc base=utc"
}

case "${1:-}" in
  install)
    [ -f "$WIN_ISO" ] || { echo "missing $WIN_ISO — run fetch-win11-arm.sh first"; exit 1; }
    [ -f "$VIRTIO_ISO" ] || { echo "missing $VIRTIO_ISO — run fetch-win11-arm.sh"; exit 1; }
    prep
    echo "==> booting unattended install (headless). Watch: $VMDIR/serial.log"
    echo "    SSH will come up on localhost:$SSH_PORT once provisioning finishes (~20-30 min)."
    exec "$QEMU" $(common_args) \
      -drive if=none,id=install,media=cdrom,file="$WIN_ISO" -device usb-storage,drive=install,bootindex=0 \
      -drive if=none,id=unattend,media=cdrom,file="$UNATTEND_ISO" -device usb-storage,drive=unattend,bootindex=2 \
      -drive if=none,id=virtio,media=cdrom,file="$VIRTIO_ISO" -device usb-storage,drive=virtio,bootindex=3
    ;;
  run)
    prep
    echo "==> booting VM (SSH on localhost:$SSH_PORT)"
    exec "$QEMU" $(common_args) \
      -drive if=none,id=unattend,media=cdrom,file="$UNATTEND_ISO" -device usb-storage,drive=unattend
    ;;
  ssh)
    shift; exec ssh -p "$SSH_PORT" -i "$VMDIR/id_vm" -o IdentitiesOnly=yes \
      -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null tester@localhost "$@"
    ;;
  stop)
    ssh -p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null tester@localhost \
      "shutdown /s /t 0" 2>/dev/null || pkill -f "serial:file:$VMDIR/serial.log" || true
    ;;
  *)
    usage; exit 0 ;;
esac
