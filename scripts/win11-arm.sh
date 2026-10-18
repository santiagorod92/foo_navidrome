#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/win11-arm.sh <prepare|up|open|provision|deploy|fb2k|seed|cfg|run|push|ssh|shot|logs|down|status|reset>

Windows 11 ARM test VM on an Apple Silicon Mac, driven like the Linux Win11 VM:
one command to start it, its screen in a browser tab (noVNC), ssh in.
The VM is QEMU with Apple's own hypervisor (HVF). Windows can't run under
colima's nested KVM (it hangs at "Start boot option"), so dockur is only used
once, by prepare, to download Windows and build its unattended setup.

  prepare  one-time: run dockurr/windows-arm in the default colima profile
           (KVM=N) until it has downloaded the Windows 11 ARM ISO and built its
           setup image (tester/tester, OpenSSH from dev/win11-arm/oem), then copy
           the ISO, the setup image and the noVNC client out of it
  up       start the VM (and the noVNC page) as launchd jobs, so it survives
           closing the terminal or the editor; the first boot installs Windows
           unattended (~20-30 min). Runs prepare first when its files are missing
  open     open the screen in the browser once it answers (http://127.0.0.1:8006)
  provision  one-time, after the install: put your ssh key in the guest and
           install foobar2000 (ARM build, FB2K_VERSION) silently
  deploy   copy build-win-mac/foo_navidrome.dll (or DLL) into the guest profile's
           user-components-arm64ec/<DLL name> and restart foobar2000 in the
           desktop session (foo_ui_panels deploys its own DLL the same way)
  fb2k     start|stop|cmd <menu command> foobar2000 in the guest's desktop session;
           fb2k log [LINES] [REGEX] prints the end of the guest's $COMPONENT.log
           (COMPONENT, default foo_navidrome)
  seed     copy foo_navidrome's settings (server, account, language, prefetch)
           from SEED_CONFIG (default: this Mac's foobar2000 profile) into the guest.
           A server on a private address is relayed: macOS doesn't let QEMU
           reach the LAN, so up adds a guestfwd to /usr/bin/nc and seed points
           the guest's hosts file at it
  cfg      run SQL on the guest's config.sqlite (foobar2000 is stopped first;
           start it again with fb2k start)
  run      run EXE [ARGS] in the guest (copied to the guest's temp folder), with
           its output and exit code
  push     push SRC DEST: copy a local file or folder to DEST (a path under the
           guest user's home, e.g. AppData/Local/Temp); a folder of the same
           name already there is replaced
  ssh      ssh into the guest as tester/tester (port 2223)
  shot     save the VM screen to FILE (default build/ui-test/win11-arm-<time>.png)
  logs     follow the VM's serial/QEMU log
  down     stop the VM and the noVNC page (the disk is kept)
  status   VM, screen and ssh state
  reset    down, then delete the Windows disk (the next up installs again)

Env: WIN11_ARM_DIR (default ~/.local/share/foo_navidrome-win11-arm), WIN11_ARM_CPUS (6),
     WIN11_ARM_MEMORY (6G), WIN11_ARM_DISK (64G), FB2K_VERSION (2.26.1),
     SEED_CONFIG (~/Library/foobar2000-v2/config.sqlite). Files go in and out with scp over ssh.
USAGE
}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="${WIN11_ARM_DIR:-$HOME/.local/share/foo_navidrome-win11-arm}"
CPUS="${WIN11_ARM_CPUS:-6}"
MEM="${WIN11_ARM_MEMORY:-6G}"
DISK_SIZE="${WIN11_ARM_DISK:-64G}"
WEB_PORT=8006
VNC_DISPLAY=5
WS_PORT=5705
SSH_PORT=2223
RDP_PORT=3390
VM_JOB=com.foo_navidrome.win11-arm.vm
WEB_JOB=com.foo_navidrome.win11-arm.web
MONITOR="$DIR/monitor.sock"
LOG="$DIR/qemu.log"
PREP=foo_navidrome-win11-arm-prepare
PREP_VOL=foo_navidrome-win11-arm-prepare
FB2K_VERSION="${FB2K_VERSION:-2.26.1}"
SSH_OPTS=(-p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
URL="http://127.0.0.1:$WEB_PORT/vnc.html?host=127.0.0.1&port=$WS_PORT&path=&autoconnect=1&resize=scale&reconnect=1"

prepared() {
  [ -f "$DIR/win11-arm64.iso" ] && [ -f "$DIR/setup.img" ] && [ -f "$DIR/novnc/vnc.html" ]
}

prepare() {
  mkdir -p "$DIR"
  colima status >/dev/null 2>&1 || colima start
  local d=(docker --context colima)
  "${d[@]}" rm -f "$PREP" >/dev/null 2>&1 || true
  echo "==> dockur: downloading Windows 11 ARM and building the setup image (no VM is installed here)"
  "${d[@]}" run -d --name "$PREP" -e VERSION=11 -e USERNAME=tester -e PASSWORD=tester -e KVM=N \
    -v "$PREP_VOL:/storage" -v "$ROOT/dev/win11-arm/oem:/oem" dockurr/windows-arm >/dev/null
  until "${d[@]}" logs "$PREP" 2>&1 | grep -qE "Booting Windows|ERROR"; do
    "${d[@]}" ps -q -f "name=^$PREP$" | grep -q . || { "${d[@]}" logs --tail 20 "$PREP"; exit 1; }
    sleep 5
  done
  "${d[@]}" logs "$PREP" 2>&1 | grep -q ERROR && { "${d[@]}" logs --tail 20 "$PREP"; exit 1; }
  "${d[@]}" stop -t 5 "$PREP" >/dev/null
  echo "==> copying the ISO, setup image and noVNC out"
  "${d[@]}" run --rm -v "$PREP_VOL:/s:ro" -v "$DIR:/out" alpine sh -c \
    'cp /s/win11arm64.iso /out/win11-arm64.iso && cp /s/setup.img /out/setup.img'
  rm -rf "$DIR/novnc"; "${d[@]}" cp "$PREP:/usr/share/novnc" "$DIR/novnc"
  "${d[@]}" rm "$PREP" >/dev/null
  "${d[@]}" volume rm "$PREP_VOL" >/dev/null
  echo "==> prepared in $DIR"
}

SEED_CONFIG="${SEED_CONFIG:-$HOME/Library/foobar2000-v2/config.sqlite}"
LAN_FWD_IP=10.0.2.100

lan_server() {
  [ -f "$SEED_CONFIG" ] || return 0
  local url host port ip
  url="$(sqlite3 "$SEED_CONFIG" "SELECT value FROM configStrings WHERE name = 'cfg_var.A1B2C3D4-1111-2222-AABB-CCDDEEFF0101';" 2>/dev/null)"
  [ -n "$url" ] || return 0
  host="${url#*://}"; host="${host%%/*}"; port="${host##*:}"
  if [ "$port" = "$host" ]; then case "$url" in https:*) port=443 ;; *) port=80 ;; esac; else host="${host%:*}"; fi
  ip="$(dscacheutil -q host -a name "$host" | awk '/^ip_address/{print $2; exit}')"
  case "$ip" in 10.*|192.168.*|172.1[6-9].*|172.2[0-9].*|172.3[01].*) echo "$host $port $ip" ;; esac
}

vm_running() { pgrep -qf "foo_navidrome-win11-arm-qemu" ; }

qemu_args() {
  local net="user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22,hostfwd=tcp:127.0.0.1:$RDP_PORT-:3389" lan
  lan="$(lan_server)"
  if [ -n "$lan" ]; then
    set -- $lan
    net+=",guestfwd=tcp:$LAN_FWD_IP:$2-cmd:/usr/bin/nc $3 $2"
  fi
  printf '%s\n' \
    -name foo_navidrome-win11-arm-qemu \
    -machine virt,highmem=on,gic-version=3 -accel hvf -cpu host -smp "$CPUS" -m "$MEM" \
    -drive "if=pflash,format=raw,readonly=on,file=$(brew --prefix qemu)/share/qemu/edk2-aarch64-code.fd" \
    -drive "if=pflash,format=raw,file=$DIR/windows.vars" \
    -device ramfb -display none -vnc "127.0.0.1:$VNC_DISPLAY,websocket=$WS_PORT" \
    -monitor "unix:$MONITOR,server,nowait" -serial "file:$DIR/serial.log" \
    -device qemu-xhci,id=xhci,p2=7,p3=7 -device usb-kbd -device usb-tablet \
    -netdev "$net" \
    -device virtio-net-pci,netdev=net0 \
    -drive "file=$DIR/win11-arm64.iso,id=boot,format=raw,readonly=on,media=cdrom,if=none" \
    -device usb-storage,drive=boot,bootindex=9,removable=on \
    -object iothread,id=io2 \
    -drive "file=$DIR/data.img,id=data3,format=raw,cache=none,discard=unmap,detect-zeroes=on,if=none" \
    -device virtio-scsi-pci,id=data3,iothread=io2 \
    -device scsi-hd,drive=data3,bus=data3.0,channel=0,scsi-id=0,lun=0,rotation_rate=1,bootindex=3 \
    -drive "file=$DIR/setup.img,id=setup,format=raw,media=disk,if=none" \
    -device usb-storage,drive=setup,removable=on \
    -audiodev coreaudio,id=snd0 -device intel-hda -device hda-output,audiodev=snd0 \
    -rtc base=localtime \
    -fw_cfg name=opt/org.tianocore/UninstallMemAttrProtocol,string=y
}

press_enter_at_boot() {
  (
    for _ in $(seq 1 20); do
      sleep 1
      [ -S "$MONITOR" ] && echo "sendkey ret" | nc -U -w 1 "$MONITOR" >/dev/null 2>&1 || true
    done
  ) &
}

up() {
  prepared || prepare
  [ -f "$DIR/windows.vars" ] || dd if=/dev/zero of="$DIR/windows.vars" bs=1m count=64 2>/dev/null
  local fresh=0
  [ -f "$DIR/data.img" ] || { echo "==> new $DISK_SIZE Windows disk (Windows installs on first boot)"; qemu-img create -q -f raw "$DIR/data.img" "$DISK_SIZE"; fresh=1; }
  if ! launchctl list "$WEB_JOB" >/dev/null 2>&1; then
    launchctl submit -l "$WEB_JOB" -o "$DIR/web.log" -e "$DIR/web.log" -- \
      "$(command -v python3)" -m http.server "$WEB_PORT" --bind 127.0.0.1 --directory "$DIR/novnc"
  fi
  if ! vm_running; then
    launchctl remove "$VM_JOB" 2>/dev/null || true
    : > "$LOG"
    local args=(); while IFS= read -r a; do args+=("$a"); done < <(qemu_args)
    launchctl submit -l "$VM_JOB" -o "$LOG" -e "$LOG" -- "$(command -v qemu-system-aarch64)" "${args[@]}"
    [ "$fresh" = 1 ] && press_enter_at_boot
    for _ in $(seq 1 30); do vm_running && break; sleep 1; done
    vm_running || { echo "QEMU didn't start, see $LOG" >&2; tail -5 "$LOG" >&2; exit 1; }
  fi
  echo "VM running. screen: http://127.0.0.1:$WEB_PORT (scripts/win11-arm.sh open), ssh: -p $SSH_PORT tester@127.0.0.1"
}

open_screen() {
  echo "waiting for the VM screen at http://127.0.0.1:$WEB_PORT ..."
  for _ in $(seq 1 30); do curl -fs -o /dev/null "http://127.0.0.1:$WEB_PORT/vnc.html" && nc -z 127.0.0.1 "$WS_PORT" && break; sleep 1; done
  nc -z 127.0.0.1 "$WS_PORT" || { echo "VM screen not answering (scripts/win11-arm.sh status)" >&2; exit 1; }
  open "$URL"
}

down() {
  if vm_running && gssh exit 2>/dev/null; then
    echo "==> shutting Windows down"
    gssh "shutdown /s /t 0" >/dev/null 2>&1 || true
    for _ in $(seq 1 90); do vm_running || break; sleep 1; done
  fi
  launchctl remove "$VM_JOB" 2>/dev/null || true
  launchctl remove "$WEB_JOB" 2>/dev/null || true
  [ -S "$MONITOR" ] && echo "quit" | nc -U -w 2 "$MONITOR" >/dev/null 2>&1 || true
  for _ in $(seq 1 10); do vm_running || break; sleep 1; done
  pkill -f "foo_navidrome-win11-arm-qemu" 2>/dev/null || true
  echo "VM stopped (disk kept in $DIR/data.img)"
}

status() {
  vm_running && echo "vm: running" || echo "vm: stopped"
  curl -fs -o /dev/null "http://127.0.0.1:$WEB_PORT/vnc.html" && echo "screen: http://127.0.0.1:$WEB_PORT" || echo "screen: down"
  local probe
  probe="$(ssh "${SSH_OPTS[@]}" -o BatchMode=yes -o ConnectTimeout=5 tester@127.0.0.1 "echo key-ok" 2>&1 || true)"
  case "$probe" in
    *key-ok*) echo "ssh: up, key login (tester@127.0.0.1 -p $SSH_PORT)" ;;
    *[Pp]ermission\ denied*) echo "ssh: up, password only (scripts/win11-arm.sh provision installs your key)" ;;
    *) echo "ssh: not answering yet" ;;
  esac
  prepared && echo "prepared: yes ($DIR)" || echo "prepared: no (up runs prepare)"
}

gssh() { ssh "${SSH_OPTS[@]}" -o BatchMode=yes tester@127.0.0.1 "$@"; }
gscp() { scp -P "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o BatchMode=yes "$@"; }
gps() { gssh "powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand $(printf '%s' "\$ProgressPreference='SilentlyContinue'; $1" | iconv -f UTF-8 -t UTF-16LE | base64)"; }

install_key() {
  gssh exit 2>/dev/null && return 0
  local key; key="$(cat "$HOME/.ssh/id_ed25519.pub" 2>/dev/null || cat "$HOME/.ssh/id_rsa.pub")"
  echo "==> installing your ssh key in the guest"
  KEY="$key" SSH_PORT="$SSH_PORT" expect <<'EXP' >/dev/null
set timeout 60
set cmd "\$k='$env(KEY)'; \$f='C:\\ProgramData\\ssh\\administrators_authorized_keys'; Set-Content -Path \$f -Value \$k -Encoding ascii; icacls \$f /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F'"
spawn ssh -p $env(SSH_PORT) -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no tester@127.0.0.1 powershell -NoProfile -Command "$cmd"
expect "password:" { send "tester\r" }
expect eof
EXP
  gssh exit || { echo "ssh key install failed" >&2; exit 1; }
}

in_session() {
  local arg=""; [ -n "${2:-}" ] && arg=" -Argument '${2//\'/\'\'}'"
  gps "\$a=New-ScheduledTaskAction -Execute '$1'$arg; \$p=New-ScheduledTaskPrincipal -UserId tester -LogonType Interactive; Register-ScheduledTask -TaskName fbnd-gui -Action \$a -Principal \$p -Force | Out-Null; Start-ScheduledTask -TaskName fbnd-gui; Start-Sleep 2; Unregister-ScheduledTask -TaskName fbnd-gui -Confirm:\$false"
}

FB2K_EXE='C:\Program Files\foobar2000\foobar2000.exe'

fb2k() {
  case "${1:-start}" in
    start) in_session "$FB2K_EXE" "${2:-}" ;;
    stop)
      [ -n "$(gps "Get-Process foobar2000 -ErrorAction SilentlyContinue")" ] || return 0
      in_session "$FB2K_EXE" "/exit"
      gps "for (\$i=0; \$i -lt 20 -and (Get-Process foobar2000 -ErrorAction SilentlyContinue); \$i++) { Start-Sleep 1 }; Stop-Process -Name foobar2000 -ErrorAction SilentlyContinue; exit 0"
      ;;
    cmd)   shift; in_session "$FB2K_EXE" "/command:\"$*\"" ;;
    log)   gps "Get-Content -Tail ${2:-40} \$env:APPDATA\\foobar2000-v2\\${COMPONENT:-foo_navidrome}.log | Where-Object { \$_ -match '${3:-.}' }" ;;
    *) usage; exit 2 ;;
  esac
}

provision() {
  install_key
  gps "if (-not (Get-NetFirewallRule -Name fbnd-foobar2000 -ErrorAction SilentlyContinue)) { New-NetFirewallRule -Name fbnd-foobar2000 -DisplayName foobar2000 -Program '$FB2K_EXE' -Action Allow -Profile Any | Out-Null }"
  if gps "Test-Path '$FB2K_EXE'" | grep True >/dev/null; then echo "foobar2000 already installed"; return; fi
  local exe="foobar2000-arm64ec_v$FB2K_VERSION.exe"
  [ -f "$DIR/$exe" ] || curl -fL -o "$DIR/$exe" "https://www.foobar2000.org/downloads/$exe"
  echo "==> installing foobar2000 $FB2K_VERSION (ARM) in the guest"
  gscp "$DIR/$exe" "tester@127.0.0.1:$exe"
  gps "Start-Process -Wait -FilePath .\\$exe -ArgumentList '/S'; Remove-Item .\\$exe"
  gps "Test-Path '$FB2K_EXE'" | grep True >/dev/null || { echo "foobar2000 install failed" >&2; exit 1; }
  echo "foobar2000 installed"
}

deploy() {
  local dll="${1:-$ROOT/build-win-mac/foo_navidrome.dll}"
  [ -f "$dll" ] || { echo "no $dll (make win-build-mac)" >&2; exit 1; }
  local name; name="$(basename "$dll" .dll)"
  fb2k stop
  gps "New-Item -ItemType Directory -Force \$env:APPDATA\\foobar2000-v2\\user-components-arm64ec\\$name | Out-Null"
  gscp "$dll" "tester@127.0.0.1:AppData/Roaming/foobar2000-v2/user-components-arm64ec/$name/$name.dll"
  fb2k start
  echo "deployed $(basename "$dll"), foobar2000 started"
}

GUEST_CONFIG="AppData/Roaming/foobar2000-v2/config.sqlite"

guest_hosts() {
  local lan; lan="$(lan_server)"
  [ -n "$lan" ] || return 0
  set -- $lan
  echo "==> guest: $1 -> $LAN_FWD_IP (relayed to $3:$2 on this Mac)"
  gps "\$f='C:\\Windows\\System32\\drivers\\etc\\hosts'; \$l = @(Get-Content \$f | Where-Object { \$_ -notmatch '\\s$1\$' }) + '$LAN_FWD_IP $1'; Set-Content -Path \$f -Value \$l -Encoding ascii; ipconfig /flushdns | Out-Null"
}

cfg() {
  local tmp; tmp="$(mktemp -d)"
  fb2k stop
  gscp "tester@127.0.0.1:$GUEST_CONFIG" "$tmp/config.sqlite"
  sqlite3 "$tmp/config.sqlite" "$1"
  gscp "$tmp/config.sqlite" "tester@127.0.0.1:$GUEST_CONFIG"
  rm -rf "$tmp"
}

seed() {
  local src="$SEED_CONFIG"
  [ -f "$src" ] || { echo "no $src" >&2; exit 1; }
  local tmp; tmp="$(mktemp -d)"
  fb2k stop
  gscp "tester@127.0.0.1:$GUEST_CONFIG" "$tmp/config.sqlite"
  local sql="ATTACH '$src' AS src;" t
  for t in configStrings configInts configBlobs configReals; do
    sql+="INSERT OR REPLACE INTO main.$t SELECT * FROM src.$t WHERE name LIKE 'cfg_var.A1B2C3D4-1111-2222-AABB-CCDDEEFF%' OR name LIKE 'foo_navidrome.%';"
  done
  sqlite3 "$tmp/config.sqlite" "$sql"
  echo "==> seeded $(sqlite3 "$tmp/config.sqlite" "SELECT count(*) FROM (SELECT name FROM configStrings UNION ALL SELECT name FROM configInts UNION ALL SELECT name FROM configBlobs) WHERE name LIKE 'cfg_var.A1B2C3D4%' OR name LIKE 'foo_navidrome.%'") settings from $src"
  gscp "$tmp/config.sqlite" "tester@127.0.0.1:$GUEST_CONFIG"
  rm -rf "$tmp"
  guest_hosts
  fb2k start
}

case "${1:-}" in
  prepare) prepare ;;
  up)      up ;;
  open)    open_screen ;;
  provision) provision ;;
  deploy)  shift; deploy "$@" ;;
  fb2k)    shift; fb2k "$@" ;;
  seed)    seed ;;
  cfg)     shift; cfg "${1:?usage: cfg SQL}" ;;
  run)
    shift; exe="${1:?usage: run EXE [ARGS]}"; shift
    gscp "$exe" "tester@127.0.0.1:AppData/Local/Temp/$(basename "$exe")"
    gssh "cd /d %TEMP% && $(basename "$exe") $*"
    ;;
  push)
    shift; src="${1:?usage: push SRC DEST}"; dest="${2:?usage: push SRC DEST}"
    gps "Remove-Item -Recurse -Force \"\$HOME\\${dest//\//\\}\\$(basename "$src")\" -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force \"\$HOME\\${dest//\//\\}\" | Out-Null"
    gscp -r "$src" "tester@127.0.0.1:$dest/"
    ;;
  ssh)     shift; exec ssh -p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null tester@127.0.0.1 "$@" ;;
  shot)
    f="${2:-$ROOT/build/ui-test/win11-arm-$(date +%H%M%S).png}"; mkdir -p "$(dirname "$f")"
    echo "screendump $f -f png" | nc -U -w 2 "$MONITOR" >/dev/null && sleep 1 && [ -f "$f" ] && echo "$f"
    ;;
  logs)    exec tail -f "$LOG" "$DIR/serial.log" ;;
  down)    down ;;
  status)  status ;;
  reset)   down; rm -f "$DIR/data.img" "$DIR/windows.vars"; echo "Windows disk deleted" ;;
  -h|--help|"") usage ;;
  *) usage; exit 2 ;;
esac
