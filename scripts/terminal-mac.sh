#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/terminal-mac.sh <up|open|view|status|provision|deploy|fb2k|seed|cfg|push|ssh|shot|click|move|drag|type|key|scroll|down>

macOS test VM on this Apple Silicon Mac, through terminal-mac (github.com/govijr/terminal-mac):
a real macOS guest (Apple's Virtualization framework) whose screen and input go through a
helper inside the guest, so a test never touches this Mac's own desktop or foobar2000. Same
commands as macos-devbox's mvm, so every repo's ui-test.sh mac drives it (MVM=this script).

  up         start the VM (terminal-mac start) and wait for its sshd
  open       show the VM's own window on this Mac (terminal-mac show)
  view       its live screen in this terminal (terminal-mac view; Ctrl+] exits)
  status     VM, guest helper and ssh state
  provision  one-time, after terminal-mac up and enabling Remote Login in the guest
             (System Settings > General > Sharing): put your ssh key in the guest
             (TERMINAL_MAC_PASSWORD, default dev123, asked once) and copy this Mac's
             foobar2000.app (FB2K_APP) to the guest's /Applications unless one is
             there, make Panels UI the UI module, put two silent tracks
             (~/Music/fb2k-test) in the default playlist, and keep the display and the
             guest awake (pmset; a sleeping display locks the session and screenshots
             fail with "No guest display is available")
  deploy     COMPONENT [--launch]: copy a .component bundle (or a .fb2k-component zip)
             into the guest profile's user-components/<name>/, foobar2000 stopped first
  fb2k       start|stop|restart foobar2000 in the guest's desktop session;
             fb2k log [LINES] [REGEX] prints the end of the guest's $COMPONENT.log
             (COMPONENT, default foo_navidrome)
  seed       copy foo_navidrome's settings (server, account, language, prefetch) from
             SEED_CONFIG (default: this Mac's foobar2000 profile) into the guest
  cfg        run SQL on the guest's config.sqlite (foobar2000 is stopped first)
  push       SRC DEST: copy a local file or folder into DEST (relative to the guest home);
             a folder of the same name already there is replaced
  ssh        ssh into the guest (or run CMD there)
  shot       save the guest screen to FILE (default ./build/ui-test/terminal-mac-<time>.png)
             and print its path
  click      X Y [right|double]   (screen points, as in shot's image at 1x)
  move       X Y
  drag       X Y X2 Y2
  type       TEXT (literal Unicode keystrokes)
  key        KEY...: return|enter, escape|esc, tab, up, down, cmd+q, ... 0.3 s apart
  scroll     X Y DY
  down       shut the guest down (the disk is kept)

Env: TERMINAL_MAC_NAME (default), TERMINAL_MAC_USER (dev), TERMINAL_MAC_HOST (the guest's
     address; default: its newest DHCP lease on bridge100), TERMINAL_MAC_PASSWORD (dev123,
     provision only), FB2K_APP (/Applications/foobar2000.app), COMPONENT (log name).
USAGE
}

NAME="${TERMINAL_MAC_NAME:-default}"
GUSER="${TERMINAL_MAC_USER:-dev}"
TM="${TERMINAL_MAC:-terminal-mac}"
PROFILE='Library/foobar2000-v2'
PANELS_UI_MODULE='6F0A1B2C-3D4E-4F50-9A1B-2C3D4E5F6071'
FB2K_APP="${FB2K_APP:-/Applications/foobar2000.app}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5)

guest_host() {
  if [ -n "${TERMINAL_MAC_HOST:-}" ]; then echo "$TERMINAL_MAC_HOST"; return; fi
  local ip="" lease best=0 out=""
  while IFS='=' read -r k v; do
    case "${k//[[:space:]]/}" in
      ip_address) ip="$v" ;;
      lease) lease=$(( v )); [ "$lease" -gt "$best" ] && { best="$lease"; out="$ip"; } ;;
    esac
  done < /var/db/dhcpd_leases 2>/dev/null || true
  echo "$out"
}

HOST="$(guest_host)"
target() { [ -n "$HOST" ] || { echo "no guest address (is the VM up? set TERMINAL_MAC_HOST)" >&2; exit 1; }; echo "$GUSER@$HOST"; }
gssh() { ssh "${SSH_OPTS[@]}" -o BatchMode=yes "$(target)" "$@"; }
ssh_ready() { [ -n "$HOST" ] && gssh true >/dev/null 2>&1; }
act() { "$TM" action "$NAME" "$@"; }

up() {
  "$TM" status "$NAME" | grep -q '"running"' || "$TM" start "$NAME"
  echo "waiting for the guest's sshd ..."
  for _ in $(seq 1 120); do
    HOST="$(guest_host)"
    ssh_ready && { echo "guest up: $(target)"; return 0; }
    sleep 3
  done
  echo "no ssh into the guest: enable Remote Login there, then run provision" >&2
  exit 1
}

status() {
  "$TM" status "$NAME"
  act status || true
  if ssh_ready; then echo "ssh: $(target) ok"; else echo "ssh: not reachable (${HOST:-no address})"; fi
}

silence_wav() {
  printf 'RIFF\x24\x3e\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x40\x1f\x00\x00\x80\x3e\x00\x00\x02\x00\x10\x00data\x00\x3e\x00\x00'
  head -c 15872 /dev/zero
}

provision() {
  [ -n "$HOST" ] || { echo "no guest address" >&2; exit 1; }
  local key="${SSH_KEY:-$HOME/.ssh/id_ed25519.pub}" pass
  [ -f "$key" ] || { echo "no $key" >&2; exit 1; }
  if ! ssh_ready; then
    pass="$(mktemp)"
    printf '#!/bin/sh\necho %q\n' "${TERMINAL_MAC_PASSWORD:-dev123}" > "$pass"; chmod 700 "$pass"
    SSH_ASKPASS="$pass" SSH_ASKPASS_REQUIRE=force ssh "${SSH_OPTS[@]}" "$(target)" \
      'mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys' < "$key"
    rm -f "$pass"
    ssh_ready || { echo "the key didn't take" >&2; exit 1; }
    echo "ssh key installed for $(target)"
  fi
  if gssh "test -d /Applications/foobar2000.app"; then echo "foobar2000 already installed"
  else
    [ -d "$FB2K_APP" ] || { echo "no $FB2K_APP on this Mac (FB2K_APP=...)" >&2; exit 1; }
    echo "==> copying $(basename "$FB2K_APP") ($(defaults read "$FB2K_APP/Contents/Info" CFBundleShortVersionString)) to the guest"
    tar -C "$(dirname "$FB2K_APP")" -cf - "$(basename "$FB2K_APP")" | gssh "tar -xf - -C /Applications && xattr -dr com.apple.quarantine /Applications/foobar2000.app 2>/dev/null; true"
  fi
  gssh "mkdir -p ~/$PROFILE/user-components"
  gssh "test -f ~/$PROFILE/config.sqlite" || { fb2k start; sleep 5; }
  fb2k stop
  gssh "sqlite3 ~/$PROFILE/config.sqlite \"insert or replace into configStrings(name,value) values('ui.module','$PANELS_UI_MODULE')\""
  if ! gssh "test -f ~/Music/fb2k-test/silence-2.wav"; then
    gssh "mkdir -p ~/Music/fb2k-test"
    local n
    for n in 1 2; do silence_wav | gssh "cat > ~/Music/fb2k-test/silence-$n.wav"; done
    fb2k start; sleep 4
    gssh "open -a foobar2000 ~/Music/fb2k-test/silence-1.wav ~/Music/fb2k-test/silence-2.wav"; sleep 3
    fb2k stop
    echo "two silent tracks in the default playlist (~/Music/fb2k-test)"
  fi
  stay_awake
  echo "provisioned: UI module = Panels UI (deploy foo_ui_panels.component next)"
}

stay_awake() {
  gssh "pmset -g | grep -qE '^ displaysleep +0\$' && pmset -g | grep -qE '^ sleep +0\$'" && return 0
  printf '%s\n' "${TERMINAL_MAC_PASSWORD:-dev123}" | gssh "sudo -S -p '' pmset -a displaysleep 0 sleep 0 disksleep 0" \
    && echo "the guest's display and system no longer sleep"
}

running() { gssh 'pgrep -x foobar2000 >/dev/null'; }

fb2k() {
  case "${1:-start}" in
    start) gssh 'open -a foobar2000' ;;
    stop)
      running || return 0
      gssh 'open -a foobar2000'; sleep 1
      act key cmd+q >/dev/null 2>&1 || true
      for _ in $(seq 1 20); do running || return 0; sleep 0.5; done
      gssh 'pkill -x foobar2000; sleep 1; pkill -9 -x foobar2000; true'
      ;;
    restart) fb2k stop; fb2k start ;;
    log) gssh "tail -n ${2:-40} ~/$PROFILE/${COMPONENT:-foo_navidrome}.log | grep -aE '${3:-.}'" ;;
    *) usage; exit 2 ;;
  esac
}

deploy() {
  local comp="${1:?usage: deploy COMPONENT [--launch]}" launch="${2:-}" name tmp=""
  [ -e "$comp" ] || { echo "no $comp" >&2; exit 1; }
  case "$comp" in
    *.fb2k-component)
      tmp="$(mktemp -d)"; unzip -q "$comp" -d "$tmp"
      comp="$(find "$tmp" -maxdepth 2 -name '*.component' -type d | head -1)"
      [ -n "$comp" ] || { echo "no .component bundle in that archive" >&2; rm -rf "$tmp"; exit 1; }
      ;;
  esac
  name="$(basename "$comp" .component)"
  fb2k stop
  gssh "rm -rf ~/$PROFILE/user-components/$name && mkdir -p ~/$PROFILE/user-components/$name"
  tar -C "$(dirname "$comp")" -cf - "$(basename "$comp")" | gssh "tar -xf - -C ~/$PROFILE/user-components/$name"
  [ -n "$tmp" ] && rm -rf "$tmp"
  if [ "$launch" = --launch ]; then fb2k start; echo "deployed $name, foobar2000 started"; else echo "deployed $name"; fi
}

push() {
  local src="${1:?usage: push SRC DEST}" dest="${2:?usage: push SRC DEST}"
  gssh "rm -rf ~/'$dest'/'$(basename "$src")' && mkdir -p ~/'$dest'"
  tar -C "$(dirname "$src")" -cf - "$(basename "$src")" | gssh "tar -xf - -C ~/'$dest'"
}

seed() {
  local src="${SEED_CONFIG:-$HOME/Library/foobar2000-v2/config.sqlite}" t sql=""
  [ -f "$src" ] || { echo "no $src" >&2; exit 1; }
  for t in configStrings configInts configBlobs configReals; do
    sql+="$(sqlite3 "$src" ".mode insert $t" "SELECT * FROM $t WHERE name LIKE 'cfg_var.A1B2C3D4-1111-2222-AABB-CCDDEEFF%' OR name LIKE 'foo_navidrome.%'" | sed 's/^INSERT INTO/INSERT OR REPLACE INTO/')"$'\n'
  done
  fb2k stop
  printf '%s' "$sql" | gssh "sqlite3 ~/$PROFILE/config.sqlite"
  echo "==> seeded $(grep -c '^INSERT' <<<"$sql") settings from $src"
}

key_name() {
  case "$1" in
    enter|ret) echo return ;;
    esc) echo escape ;;
    *) echo "$1" ;;
  esac
}

case "${1:-}" in
  up)        up ;;
  open)      "$TM" show "$NAME" ;;
  view)      exec "$TM" view "$NAME" ;;
  status)    status ;;
  provision) provision ;;
  deploy)    shift; deploy "$@" ;;
  fb2k)      shift; fb2k "$@" ;;
  seed)      seed ;;
  cfg)       shift; fb2k stop; gssh "sqlite3 ~/$PROFILE/config.sqlite \"${1:?usage: cfg SQL}\"" ;;
  push)      shift; push "$@" ;;
  ssh)       shift; exec ssh "${SSH_OPTS[@]}" "$(target)" "$@" ;;
  shot)
    f="${2:-$PWD/build/ui-test/terminal-mac-$(date +%H%M%S).png}"; mkdir -p "$(dirname "$f")"
    act screenshot --output "$f" >/dev/null && echo "$f"
    ;;
  click)
    shift; x="${1:?usage: click X Y [right|double]}"; y="${2:?usage: click X Y [right|double]}"
    case "${3:-}" in
      right)  act click "$x" "$y" --button right ;;
      double) act click "$x" "$y" --double ;;
      *)      act click "$x" "$y" ;;
    esac
    ;;
  move)      shift; act move "${1:?usage: move X Y}" "${2:?usage: move X Y}" ;;
  drag)      shift; act drag "${1:?usage: drag X Y X2 Y2}" "${2:?}" "${3:?}" "${4:?}" ;;
  type)      shift; act type "$*" ;;
  key)       shift; [ $# -gt 0 ] || { echo "usage: key KEY..." >&2; exit 2; }; for k in "$@"; do act key "$(key_name "$k")" >/dev/null; sleep 0.3; done ;;
  scroll)    shift; act scroll --x "${1:?usage: scroll X Y DY}" --y "${2:?}" --dy "${3:?}" ;;
  down)      "$TM" stop "$NAME" ;;
  -h|--help|"") usage ;;
  *) usage; exit 2 ;;
esac
