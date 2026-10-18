#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: ui-test.sh <platform> <scenario> [args]
       ui-test.sh compare <scenario> [platform...]

Drives the real foobar2000 through the same scenarios on every platform and asserts on the
component's debug log, the process and its crash reports; screenshots go to build/ui-test/.

Platforms
  wine     the local Wine foobar2000 (~/.foobar2000); input posted with tools/wclick.c
  win11    the Windows 11 VM (../windows-devbox, wvm); real Windows, VNC input
  mac      the macOS VM (../macos-devbox, mvm); VNC input

Scenarios (one implementation each, run on any platform that supports it)
  smoke [ARTIFACT]      relaunch, open the browser, expand an artist, play an album, post-checks
  playqueue [ARTIFACT]  transcode (PLAYQUEUE_FORMAT, default opus), play an album, File > Save
                        Play Queue to Navidrome (asserts the confirmation: a toast where foobar2000
                        has one, else a dialog that Enter must close), File > Restore Play Queue from Navidrome; asserts the restore
                        resumes at the saved position, by reopening the stream at timeOffset or by
                        a decoder seek, whichever applies (the server caches transcodes)
  search [ARTIFACT] [Q] wine, win11: search Q (default "the") and clear it; asserts both tree
                        rebuilds stay under SEARCH_MAX_MS (default 500) and prints their timings
  cui [ARTIFACT]        win11: switch the UI module to Columns UI for the run (restored after),
                        assert the Navidrome Browser panel loads (one-time setup: Columns UI
                        installed, the panel added to its layout via Live layout editing)
  prefs [PAGE]          wine, win11: Preferences on one of our pages, screenshot
                        (main | audiomuse | libraries | sections | radio | media | components)

ARTIFACT: what gets deployed first. wine: nothing (make win-build installs into the Wine
profile); win11: a DLL (default build-win/foo_navidrome.dll); mac: a .fb2k-component (default:
the newest one in the repo root). Pass "-" to skip the deploy.

Helpers
  browser               open the Navidrome Browser in the running foobar2000
  log [N]               last N lines of the platform's debug log
  shot [FILE]           screenshot
  key NAME              press a key in the browser (end, right, down, enter, tab, bsp, ...)
  cmd "MENU COMMAND"    run a main-menu command
  seed                  win11: copy foo_navidrome's settings from the Wine profile into the guest
  click X Y [ARGS...]   wine: raw tools/wclick.c call (client coordinates of the browser)

compare <scenario> [platform...]
  runs the scenario on each platform (default: wine win11) and prints a step-by-step matrix
  of ok / FAIL / not reached, plus the notes each run recorded (e.g. which seek path ran).
  PARALLEL=1 runs the platforms at the same time (they are separate machines; the one shared
  thing, the user's play queue on the server, is serialized with a lock in playqueue)
USAGE
}

set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SHOTS="$REPO/build/ui-test"
GUID_PREFIX='A1B2C3D4-1111-2222-AABB-CCDDEEFF'
STREAM_FORMAT_KEY="cfg_var.${GUID_PREFIX}010C"
FIRST_ROW_X=60; FIRST_ROW_Y=43
SEARCH_X=200; SEARCH_Y=15

say()  { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; record ok "$*"; }
warn() { printf '\033[1;33m  !!\033[0m %s\n' "$*"; }
note() { printf '\033[1;35m  ..\033[0m %s: %s\n' "$1" "$2"; [ -n "${RESULTS:-}" ] && printf 'note\t%s\t%s\n' "$1" "$2" >> "$RESULTS"; return 0; }
fail() {
  printf '\033[1;31mFAIL\033[0m %s\n' "$*"; record FAIL "$*"
  d_shot "$SHOTS/$PLATFORM-fail-$(date +%H%M%S).png" >/dev/null 2>&1 || true
  exit 1
}
record() { [ -n "${RESULTS:-}" ] && printf '%s\t%s\n' "$1" "${2%% — *}" >> "$RESULTS"; return 0; }

pages() {
  case "${1:-main}" in
    main) echo "${GUID_PREFIX}0105" ;; audiomuse) echo "${GUID_PREFIX}0405" ;;
    libraries) echo "${GUID_PREFIX}0112" ;; sections) echo "${GUID_PREFIX}0117" ;;
    radio) echo "${GUID_PREFIX}010F" ;; media) echo "${GUID_PREFIX}0109" ;;
    components) echo '0E966267-7DFB-433B-A07C-3F8CDD31A258' ;;
    *) fail "unknown page '$1' (main | audiomuse | libraries | sections | radio | media | components)" ;;
  esac
}

wine_init() {
  PREFIX="${WINEPREFIX:-$HOME/.local/share/wineprefixes/foobar2000}"
  LOG="${NAVIDROME_DEBUG_LOG_FILE:-/tmp/foo_navidrome_debug.log}"
  WINE_CFG="$HOME/.foobar2000/profile/config.sqlite"
  XWIN="${XWIN_SDK:-$HOME/.local/share/xwin/sdk}"
  WCLICK_DIR="$REPO/build/tools"; WCLICK="$WCLICK_DIR/wclick.exe"
  SUPPORTS="smoke playqueue search prefs"
}
build_wclick() {
  [ "$WCLICK" -nt "$REPO/tools/wclick.c" ] && return
  mkdir -p "$WCLICK_DIR"
  clang-cl --target=x86_64-pc-windows-msvc /nologo /O1 /MT /c "$REPO/tools/wclick.c" /Fo"$WCLICK_DIR/wclick.obj" \
    /imsvc"$XWIN/crt/include" /imsvc"$XWIN/sdk/include/ucrt" /imsvc"$XWIN/sdk/include/um" /imsvc"$XWIN/sdk/include/shared" 2>&1 | grep -v msvc-not-found || true
  lld-link /nologo "$WCLICK_DIR/wclick.obj" /libpath:"$XWIN/crt/lib/x86_64" /libpath:"$XWIN/sdk/lib/um/x86_64" \
    /libpath:"$XWIN/sdk/lib/ucrt/x86_64" user32.lib /out:"$WCLICK"
}
wclick() { build_wclick; WINEPREFIX="$PREFIX" WINEDEBUG=-all wine "$WCLICK" "$@"; }
wine_vk() {
  case "$1" in
    enter) echo 0x0D ;; end) echo 0x23 ;; home) echo 0x24 ;; left) echo 0x25 ;; up) echo 0x26 ;;
    right) echo 0x27 ;; down) echo 0x28 ;; tab) echo 0x09 ;; bsp) echo 0x08 ;; esc) echo 0x1B ;;
    0x*) echo "$1" ;; *) fail "no virtual-key code for '$1'" ;;
  esac
}
geom() {
  hyprctl clients -j | python3 -c "import json,sys
t = sys.argv[1]
cs = [c for c in json.load(sys.stdin) if c['class'].lower() == 'foobar2000.exe' and t in c['title']]
if not cs: sys.exit(1)
c = max(cs, key=lambda c: c['size'][0] * c['size'][1])
print('%d,%d %dx%d' % (c['at'][0], c['at'][1], c['size'][0], c['size'][1]))" "${1-Navidrome Browser}"
}
wine_deploy() { [ -n "${1:-}" ] && [ "$1" != - ] && warn "wine installs with make win-build; ignoring $1"; return 0; }
wine_running() { pgrep -f 'foobar2000[.]exe' >/dev/null; }
wine_stop() {
  wine_running || return 0
  foobar2000 -exit >/dev/null 2>&1 || true
  for _ in $(seq 1 20); do wine_running || break; sleep 0.5; done
  pkill -f 'foobar2000[.]exe' || true; sleep 1
}
wine_start() {
  nohup foobar2000 "$@" >/dev/null 2>&1 &
  for _ in $(seq 1 40); do wine_running && break; sleep 0.5; done
}
wine_log_size() { wc -c < "$LOG" 2>/dev/null || echo 0; }
wine_log_from() { tail -c +"$1" "$LOG" 2>/dev/null || true; }
wine_clear_log() { : > "$LOG" 2>/dev/null || true; }
wine_crash_count() { ls "$HOME/.foobar2000/profile/crash reports"/*.txt 2>/dev/null | wc -l; }
wine_cfg_sql() { wine_stop; sqlite3 "$WINE_CFG" "$1"; }
wine_open_browser() {
  foobar2000 "-command:Open Navidrome Browser" >/dev/null 2>&1 &
  for _ in $(seq 1 30); do geom >/dev/null 2>&1 && return 0; sleep 0.5; done
  fail "the Navidrome Browser window didn't open"
}
wine_key() { wclick 120 200 "key:$(wine_vk "$1")" >/dev/null; sleep 0.4; }
wine_type_search() { wclick "$SEARCH_X" "$SEARCH_Y" l >/dev/null; sleep 0.3; wclick "$SEARCH_X" "$SEARCH_Y" "type:$1" >/dev/null; }
wine_erase_search() { local b; b="$(printf '\b%.0s' $(seq 1 "$1"))"; wclick "$SEARCH_X" "$SEARCH_Y" "type:$b" >/dev/null; }
wine_focus_tree() { wclick "$FIRST_ROW_X" "$FIRST_ROW_Y" l >/dev/null; sleep 0.5; }
wine_expand_artist() { wine_focus_tree; wine_key end; wine_key right; }
wine_cmd() { foobar2000 "-command:$1" >/dev/null 2>&1 & }
wine_window_exists() { geom "$1" >/dev/null 2>&1; }
wine_press_in_window() { wclick 0 0 "top:$1" "key:$(wine_vk "$2")" >/dev/null 2>&1 || true; }
wine_close_window() { wclick 0 0 "top:$1" close >/dev/null 2>&1 || true; }
wine_shot() {
  local f="$1" g
  mkdir -p "$(dirname "$f")"
  g="$(geom "${2-Navidrome Browser}" || geom '')" || { echo "no foobar2000 window to capture"; return 1; }
  grim -g "$g" "$f" && echo "$f"
}
wine_prefs() {
  local guid; guid="$(pages "${1:-main}")"
  wine_cfg_sql "INSERT OR REPLACE INTO configStrings VALUES ('preferences.lastOpenPage', '$guid');"
  wine_start -config
  for _ in $(seq 1 40); do geom Preferences >/dev/null 2>&1 && break; sleep 0.5; done
  geom Preferences >/dev/null 2>&1 || fail "Preferences didn't open"
  sleep 2
  wine_shot "$SHOTS/wine-prefs-${1:-main}-$(date +%H%M%S).png" Preferences
}

win11_init() {
  WVM="${WVM:-$(cd "$REPO/.." && pwd)/windows-devbox/wvm}"
  [ -x "$WVM" ] || { echo "wvm not found at $WVM (clone windows-devbox next to this repo)"; exit 1; }
  LOG="$("$WVM" shared)/tmp/foo_navidrome_debug.log"
  WINE_CFG="${WINE_CFG:-$HOME/.foobar2000/profile/config.sqlite}"
  GUEST_PROFILE='AppData/Roaming/foobar2000-v2'
  SUPPORTS="smoke playqueue search cui prefs"
}
win11_deploy() {
  local dll="${1:-$REPO/build-win/foo_navidrome.dll}"
  [ "$dll" = - ] && return 0
  [ -f "$dll" ] || fail "no $dll — build it first (make win-build)"
  say "deploy $(basename "$dll")"
  "$WVM" deploy "$dll" >/dev/null
}
win11_running() { "$WVM" ps 'if (Get-Process foobar2000 -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }' 2>/dev/null; }
win11_stop() { "$WVM" fb2k stop >/dev/null 2>&1 || true; }
win11_start() { "$WVM" fb2k start "$@" >/dev/null; for _ in $(seq 1 30); do win11_running && break; sleep 1; done; }
win11_log_size() { wc -c < "$LOG" 2>/dev/null || echo 0; }
win11_log_from() { tail -c +"$1" "$LOG" 2>/dev/null || true; }
win11_clear_log() { mkdir -p "$(dirname "$LOG")"; : > "$LOG"; }
win11_crash_count() {
  "$WVM" ps "(Get-ChildItem \"\$env:APPDATA\\foobar2000-v2\\crash reports\" -Filter *.txt -ErrorAction SilentlyContinue | Measure-Object).Count" | tr -d '\r '
}
win11_cfg_sql() {
  local tmp; tmp="$(mktemp -d)"
  win11_stop
  if ! "$WVM" pull "$GUEST_PROFILE/config.sqlite" "$tmp" >/dev/null 2>&1; then
    say "no guest config yet — starting foobar2000 once to create it"
    "$WVM" fb2k start >/dev/null; sleep 8; win11_stop
    "$WVM" pull "$GUEST_PROFILE/config.sqlite" "$tmp" >/dev/null || fail "the guest foobar2000 wrote no config.sqlite"
  fi
  if [ "${ATTACH_WINE:-0}" = 1 ]; then
    sqlite3 "$tmp/config.sqlite" "ATTACH 'file:$WINE_CFG?mode=ro' AS wine; $1"
  else
    sqlite3 "$tmp/config.sqlite" "$1"
  fi
  case "$1" in SELECT*) ;; *) "$WVM" push "$tmp/config.sqlite" "$GUEST_PROFILE" >/dev/null ;; esac
  rm -rf "$tmp"
}
win11_window() { "$WVM" window "$1" 2>/dev/null | awk 'NF >= 5 { print $1, $2, $3, $4, $5; exit }'; }
win11_wait_window() {
  local deadline=$(( SECONDS + ${2:-20} )) w
  until w="$(win11_window "$1")" && [ -n "$w" ]; do
    [ "$SECONDS" -lt "$deadline" ] || return 1
    sleep 1
  done
  echo "$w"
}
win11_click_client() {
  local x y w h dpi
  read -r x y w h dpi <<<"$BROWSER"
  "$WVM" click $(( x + $1 * dpi / 96 )) $(( y + $2 * dpi / 96 ))
}
win11_open_browser() {
  "$WVM" fb2k cmd "Open Navidrome Browser" >/dev/null
  BROWSER="$(win11_wait_window "Navidrome Browser" 30)" || fail "the Navidrome Browser window didn't open"
}
win11_key() { "$WVM" key "$1" >/dev/null; sleep 0.4; }
win11_focus_tree() { win11_click_client "$FIRST_ROW_X" "$FIRST_ROW_Y"; sleep 0.5; }
win11_expand_artist() { win11_focus_tree; win11_key end; win11_key right; }
win11_type_search() { win11_click_client "$SEARCH_X" "$SEARCH_Y"; sleep 0.3; "$WVM" type "$1" >/dev/null; }
win11_erase_search() { for _ in $(seq 1 "$1"); do "$WVM" key bsp >/dev/null; done; }
win11_cmd() { "$WVM" fb2k cmd "$1" >/dev/null; }
win11_window_exists() { [ -n "$(win11_window "$1")" ]; }
win11_press_in_window() { win11_key "$2"; }
win11_close_window() { "$WVM" key alt-f4 >/dev/null 2>&1 || true; }
win11_shot() { mkdir -p "$(dirname "$1")"; "$WVM" shot "$1" >/dev/null && echo "$1"; }
win11_prefs() {
  local guid; guid="$(pages "${1:-main}")"
  win11_cfg_sql "INSERT OR REPLACE INTO main.configStrings VALUES ('preferences.lastOpenPage', '$guid');"
  "$WVM" fb2k start '/config' >/dev/null
  win11_wait_window "Preferences" 30 >/dev/null || fail "Preferences didn't open"
  sleep 2
  win11_shot "$SHOTS/win11-prefs-${1:-main}-$(date +%H%M%S).png"
}
win11_seed() {
  [ -f "$WINE_CFG" ] || fail "no Wine profile at $WINE_CFG (WINE_CFG=...)"
  local n
  n="$(sqlite3 "$WINE_CFG" "SELECT COUNT(*) FROM configStrings WHERE name LIKE 'cfg_var.$GUID_PREFIX%'")"
  [ "$n" -gt 0 ] || fail "the Wine profile has no foo_navidrome settings to copy"
  say "copying $n foo_navidrome settings from the Wine profile into the guest"
  ATTACH_WINE=1 win11_cfg_sql "
    INSERT OR REPLACE INTO main.configStrings SELECT * FROM wine.configStrings WHERE name LIKE 'cfg_var.$GUID_PREFIX%';
    INSERT OR REPLACE INTO main.configInts    SELECT * FROM wine.configInts    WHERE name LIKE 'cfg_var.$GUID_PREFIX%';
    INSERT OR REPLACE INTO main.configBlobs   SELECT * FROM wine.configBlobs   WHERE name LIKE 'cfg_var.$GUID_PREFIX%';"
  ok "settings copied"
}

mac_init() {
  MVM="${MVM:-$(cd "$REPO/.." && pwd)/macos-devbox/mvm}"
  [ -x "$MVM" ] || { echo "mvm not found at $MVM"; exit 1; }
  LOG=/tmp/foo_navidrome_debug.log
  GUEST_CFG='~/Library/foobar2000-v2/config.sqlite'
  FILE_MENU_X=162; FILE_MENU_Y=11
  CATEGORY_ROWS=12
  SUPPORTS="smoke playqueue"
}
g() { "$MVM" ssh "$@"; }
mac_deploy() {
  local comp="${1:-$(ls -t "$REPO"/foo_navidrome*.fb2k-component 2>/dev/null | grep -v '_win-' | head -1)}"
  [ "$comp" = - ] && return 0
  [ -f "$comp" ] || fail "no macOS .fb2k-component — make mac-vm-build ARGS=--debug-log, or pass one"
  say "deploy $(basename "$comp")"
  mac_stop
  "$MVM" deploy "$comp" >/dev/null
}
mac_running() { g 'pgrep -x foobar2000 >/dev/null'; }
mac_stop() { g "pkill -x foobar2000 || true" >/dev/null; for _ in $(seq 1 10); do mac_running || break; sleep 1; done; }
mac_start() { g "open -a foobar2000" >/dev/null; for _ in $(seq 1 30); do mac_running && break; sleep 1; done; }
mac_log_size() { g "wc -c < $LOG 2>/dev/null || echo 0" | tr -d ' '; }
mac_log_from() { g "tail -c +$1 $LOG 2>/dev/null" || true; }
mac_clear_log() { g ": > $LOG"; }
mac_crash_count() { g 'ls ~/Library/Logs/DiagnosticReports 2>/dev/null | grep -c "^foobar2000" || true'; }
mac_cfg_sql() { mac_stop; g "sqlite3 $GUEST_CFG \"$1\""; }
mac_keys() { "$MVM" key "$@" >/dev/null; sleep 0.3; }
mac_file_menu() {
  g "osascript -e 'tell application \"foobar2000\" to activate'" >/dev/null
  sleep 1
  "$MVM" click "$FILE_MENU_X" "$FILE_MENU_Y" >/dev/null; sleep 1
  "$MVM" type "$1" >/dev/null; mac_keys enter
}
mac_open_browser() { mac_file_menu "Open N"; sleep 2; }
mac_key() { mac_keys "$1"; }
mac_expand_artist() {
  mac_keys tab
  for _ in $(seq 1 $(( CATEGORY_ROWS + 1 ))); do "$MVM" key down >/dev/null; done
  mac_keys right
}
mac_cmd() { mac_file_menu "$1"; }
mac_window_exists() { return 2; }
mac_press_in_window() { mac_keys "$2"; }
mac_close_window() { mac_keys esc; }
mac_shot() { local src; src="$("$MVM" shot | tail -1)"; mkdir -p "$(dirname "$1")"; cp "$src" "$1" && echo "$1"; }

d() { local fn="${PLATFORM}_$1"; shift; "$fn" "$@"; }
d_shot() { d shot "$@"; }
running() { d running; }

mark() { MARK="$(d log_size)"; }
since_mark() { d log_from "$(( ${MARK:-0} + 1 ))"; }
wait_log() {
  local re="$1" deadline=$(( SECONDS + ${2:-20} ))
  until since_mark | grep -Eq -- "$re"; do
    [ "$SECONDS" -lt "$deadline" ] || return 1
    running || fail "foobar2000 exited while waiting for /$re/ — crashed?"
    sleep 0.5
  done
}
expect() {
  [ "$HAVE_LOG" = 1 ] || return 0
  wait_log "$1" "$2" && ok "$3" || { since_mark | tail -15; fail "$3 — no /$1/ in the log within $2s"; }
}
seen() { [ "$HAVE_LOG" = 1 ] && since_mark | grep -Eq -- "$1"; }

supports() { case " $SUPPORTS " in *" $1 "*) return 0 ;; esac; fail "$1 isn't supported on $PLATFORM (supported: $SUPPORTS)"; }

restart() {
  say "restart foobar2000 (fresh log)"
  d stop
  d clear_log
  mark
  d start "$@"
  running || fail "foobar2000 didn't start"
  sleep 6
  CRASHES0="$(d crash_count)"
  if grep -q ' Env ' <<<"$(d log_from 1)"; then
    HAVE_LOG=1; ok "component loaded (debug log active)"
  elif [ "$PLATFORM" = mac ]; then
    HAVE_LOG=0; warn "no debug log in the guest — release build? log assertions skipped (make mac-vm-build ARGS=--debug-log)"
  else
    HAVE_LOG=1; fail "component loaded (debug log active) — nothing in $LOG; not a NAVIDROME_DEBUG_LOG build?"
  fi
}

set_stream_format() {
  if [ -n "$1" ]; then
    d cfg_sql "INSERT OR REPLACE INTO configStrings VALUES ('$STREAM_FORMAT_KEY', '$1');"
  else
    d cfg_sql "DELETE FROM configStrings WHERE name = '$STREAM_FORMAT_KEY';"
  fi
}

open_browser() {
  say "open the browser"
  mark; d open_browser
  expect 'getArtists\.view' 20 "artist list requested"
  expect '200 OK' 20 "artist list loaded"
  sleep 1
}

play_first_album() {
  open_browser
  say "expand an artist"
  mark; d expand_artist
  expect 'getArtist\.view' 20 "artist's albums requested"
  expect '200 OK' 20 "albums loaded"
  sleep 1
  say "play its first album (Down x3, Enter = replace playlist + play)"
  mark; d key down; d key down; d key down; d key enter
  expect 'queueNodes: .*play=1' 15 "Enter queued the selection for playback"
  expect 'getAlbum\.view' 20 "album songs requested"
  expect 'decode_initialize' 30 "playback reached the navidrome:// input"
  expect 'decoder ready' 30 "decoder opened the stream"
}

post_checks() {
  say "post-checks"
  sleep 2
  running || fail "foobar2000 still running"
  ok "foobar2000 still running"
  [ "$(d crash_count)" = "$CRASHES0" ] && ok "no new crash report" || fail "no new crash report — foobar2000 wrote one"
  if [ "$HAVE_LOG" = 1 ]; then
    local errs; errs="$(d log_from 1 | grep -cE '^[0-9:.]+  ERROR ' || true)"
    [ "${errs:-0}" = 0 ] && ok "no ERROR lines in the log" \
      || { d log_from 1 | grep -E '^[0-9:.]+  ERROR ' | tail -5; fail "no ERROR lines in the log — found $errs"; }
  fi
  d shot "$SHOTS/$PLATFORM-$1-$(date +%Y%m%d-%H%M%S).png" >/dev/null && ok "screenshot"
}

scenario_smoke() {
  d deploy "${1:-}"
  restart
  play_first_album
  post_checks smoke
}

scenario_playqueue() {
  local fmt="${PLAYQUEUE_FORMAT:-opus}" saved
  d deploy "${1:-}"
  saved="$(d cfg_sql "SELECT value FROM configStrings WHERE name = '$STREAM_FORMAT_KEY';" 2>/dev/null || true)"
  say "transcode to $fmt"
  set_stream_format "$fmt"
  trap "set_stream_format '$saved' >/dev/null 2>&1" EXIT
  restart
  play_first_album
  sleep 1
  if seen 'seek by timeOffset enabled'; then note "stream" "live transcode, seek by reopening at timeOffset"
  else note "stream" "seekable (transcode cached by the server), decoder seek"; fi

  say "wait for the server's play queue (one platform at a time: it's per user)"
  exec 9>"${TMPDIR:-/tmp}/foo_navidrome-playqueue.lock"
  flock -w 600 9 || fail "server play queue lock — another run held it for 10 minutes"
  ok "server play queue lock taken"

  say "File > Save Play Queue to Navidrome"
  sleep 4
  mark; d cmd "Save Play Queue to Navidrome"
  expect 'PlayQueue.*save: [0-9]+ ids, current=[^ ]+ positionMs=[0-9]' 20 "queue saved with the playing track and position"
  expect 'savePlayQueue\.view' 20 "savePlayQueue request sent"
  expect 'confirmation: (toast|dialog)' 15 "save confirmed to the user"
  local how; how="$(since_mark | grep -oE 'confirmation: (toast|dialog)' | tail -1 | cut -d' ' -f2)"
  note "confirmation" "$how"
  d shot "$SHOTS/$PLATFORM-playqueue-saved-$(date +%Y%m%d-%H%M%S).png" >/dev/null && ok "screenshot (confirmation)"
  if [ "$how" = dialog ]; then
    local shown=1
    for _ in $(seq 1 20); do d window_exists "Save Play Queue"; shown=$?; [ "$shown" = 1 ] || break; sleep 0.5; done
    case "$shown" in
      0) ok "confirmation popup shown" ;;
      2) warn "can't query windows on $PLATFORM; the popup is checked on the screenshot only" ;;
      *) fail "confirmation popup shown — no 'Save Play Queue' window" ;;
    esac
    d press_in_window "Save Play Queue" enter; sleep 1
    if [ "$shown" = 0 ]; then
      if d window_exists "Save Play Queue"; then d close_window "Save Play Queue"; fail "Enter closed the popup — still open"; fi
      ok "Enter closed the popup"
    fi
  fi

  say "File > Restore Play Queue from Navidrome"
  mark; d cmd "Restore Play Queue from Navidrome"
  expect 'getPlayQueue\.view' 20 "getPlayQueue request sent"
  expect 'PlayQueue.*restored [0-9]+ tracks' 20 "queue restored into a playlist"
  expect 'resume: seeking to [0-9]' 20 "resume seeks to the saved position"
  if seen 'seek by timeOffset enabled'; then
    expect 'seek by reopening at timeOffset=[0-9]' 20 "the seek reopens the stream"
    local offset; offset="$(since_mark | grep -oE 'seek by reopening at timeOffset=[0-9]+' | tail -1 | grep -oE '[0-9]+$')"
    note "reopen offset" "${offset}s"
    if [ "${offset:-0}" -ge 1 ]; then
      expect "stream URL = .*timeOffset=$offset" 10 "the reopened stream carries timeOffset"
    else
      ok "the reopened stream carries timeOffset"; note "timeOffset" "omitted below 1s, by design"
    fi
    expect 'decoder ready' 30 "reopened decoder ready"
  else
    expect 'seek (to [0-9.]+s \(decoder\)|by reopening at timeOffset=[0-9])' 20 "the input handled the seek"
  fi
  flock -u 9
  post_checks playqueue
}

rebuild_ms() {
  since_mark | grep -E "UI +.*$1 .* took " | tail -1 | sed -E 's/.* took ([0-9.]+)(ms|us)$/\1 \2/' |
    awk '{ printf "%d\n", ($2 == "us") ? $1 / 1000 : $1 }'
}
check_rebuild() {
  local ms max="${SEARCH_MAX_MS:-500}"
  ms="$(rebuild_ms "$1")"
  [ -n "$ms" ] || fail "$1 timed — no timing in the log"
  note "$1" "${ms}ms"
  [ "$ms" -le "$max" ] && ok "$1 under ${max}ms" || fail "$1 under ${max}ms — took ${ms}ms"
}

scenario_search() {
  local query="${2:-the}"
  d deploy "${1:-}"
  restart
  say "open the browser, select the first row"
  mark; d open_browser
  expect 'populateRoot .* took' 30 "root tree populated"
  d focus_tree

  say "search '$query'"
  mark; d type_search "$query"
  expect 'populateSearchResults .* took' 20 "search results shown"
  sleep 1
  check_rebuild populateSearchResults

  say "clear the search"
  mark; d erase_search "${#query}"
  expect 'restoreBrowseTree .* took' 20 "browse tree restored"
  check_rebuild restoreBrowseTree
  post_checks search
}

COLUMNS_UI_GUID_HEX='240A2DF1A4A8184696596F66DE067524'
UI_MODULE_KEY='cfg_var.094A28DA-C6E8-4392-BAF1-AD0F29E5B303'
UI_MODULE_KEY2='cfg_var.22AF8F41-D22D-4529-B0ED-437BE9AEC30C'
set_ui_module() {
  win11_cfg_sql "
    INSERT OR REPLACE INTO main.configBlobs VALUES ('$UI_MODULE_KEY', X'$1');
    INSERT OR REPLACE INTO main.configBlobs VALUES ('$UI_MODULE_KEY2', X'$1');"
}
scenario_cui() {
  local prev
  d deploy "${1:-}"
  prev="$(win11_cfg_sql "SELECT hex(value) FROM main.configBlobs WHERE name = '$UI_MODULE_KEY';")"
  if [ "$prev" != "$COLUMNS_UI_GUID_HEX" ]; then
    say "switch the UI module to Columns UI (was ${prev:-unset}; restored on exit)"
    set_ui_module "$COLUMNS_UI_GUID_HEX"
    [ -n "$prev" ] && trap "say 'restore the UI module'; set_ui_module '$prev'" EXIT
  fi
  restart
  expect 'Columns UI panel instantiated' 20 "Columns UI created the Navidrome Browser panel"
  expect 'getArtists\.view' 15 "artist list requested"
  expect '200 OK' 20 "artist list loaded"
  expect 'Columns UI lyrics panel instantiated' 10 "Columns UI created the Navidrome Lyrics panel"
  d cmd "Play"
  expect 'panel shows [0-9]+ line' 30 "lyrics panel showed the playing track's lyrics"
  d shot "$SHOTS/$PLATFORM-cui-lyrics.png"
  post_checks cui
}

scenario_prefs() { d prefs "${1:-main}"; }

compare() {
  local scenario="${1:-}"; shift || true
  [ -n "$scenario" ] || { usage; exit 2; }
  local platforms=("$@"); [ ${#platforms[@]} -gt 0 ] || platforms=(wine win11)
  local dir; dir="$(mktemp -d)"
  if [ "${PARALLEL:-0}" = 1 ]; then
    for p in "${platforms[@]}"; do
      ( RESULTS="$dir/$p.tsv" "$0" "$p" "$scenario" > "$dir/$p.out" 2>&1; echo "$?" > "$dir/$p.exit" ) &
    done
    wait
    for p in "${platforms[@]}"; do
      printf '\n\033[1;36m######## %s: %s ########\033[0m\n' "$p" "$scenario"; cat "$dir/$p.out"
    done
  else
    for p in "${platforms[@]}"; do
      printf '\n\033[1;36m######## %s: %s ########\033[0m\n' "$p" "$scenario"
      RESULTS="$dir/$p.tsv" "$0" "$p" "$scenario"; echo "$?" > "$dir/$p.exit"
    done
  fi
  printf '\n\033[1;36m==> %s: comparison\033[0m\n' "$scenario"
  python3 - "$dir" "${platforms[@]}" <<'PY'
import os, sys
d, plats = sys.argv[1], sys.argv[2:]
steps, notes, res = [], [], {}
for p in plats:
    res[p] = {}
    path = os.path.join(d, p + ".tsv")
    for line in (open(path).read().splitlines() if os.path.exists(path) else []):
        parts = line.split("\t")
        if parts[0] == "note":
            key = "note: " + parts[1]
            if key not in notes: notes.append(key)
            res[p][key] = parts[2]
        else:
            if parts[1] not in steps: steps.append(parts[1])
            res[p][parts[1]] = parts[0]
w = max([len(s) for s in steps + notes] + [10])
cw = max([len(p) for p in plats] + [8])
print("  " + "step".ljust(w) + "  " + "  ".join(p.ljust(cw) for p in plats))
for s in steps:
    cells = [res[p].get(s, "-") for p in plats]
    print(("* " if len(set(cells)) > 1 else "  ") + s.ljust(w) + "  " + "  ".join(c.ljust(cw) for c in cells))
for n in notes:
    cells = [res[p].get(n, "-") for p in plats]
    print(("* " if len(set(cells)) > 1 else "  ") + n.ljust(w) + "  " + " | ".join(cells))
codes = [open(os.path.join(d, p + ".exit")).read().strip() for p in plats]
print("\n  " + "  ".join("%s: %s" % (p, "passed" if c == "0" else "FAILED") for p, c in zip(plats, codes)))
print("  (* = the platforms differ on this row; - = step not reached)")
sys.exit(0 if all(c == "0" for c in codes) else 1)
PY
  local rc=$?; rm -rf "$dir"; return $rc
}

[ "${1:-}" = compare ] && { shift; compare "$@"; exit $?; }
PLATFORM="${1:-}"; shift || true
case "$PLATFORM" in
  wine|win11|mac) "${PLATFORM}_init" ;;
  *) usage; exit 2 ;;
esac
HAVE_LOG=1
CMD="${1:-}"; shift || true
case "$CMD" in
  smoke|playqueue|search|cui|prefs)
    supports "$CMD"; "scenario_$CMD" "$@"; say "$PLATFORM $CMD passed" ;;
  browser) d open_browser ;;
  log)     d log_from 1 | tail -n "${1:-50}" ;;
  shot)    d shot "${1:-$SHOTS/$PLATFORM-$(date +%H%M%S).png}" ;;
  key)     d key "$1" ;;
  cmd)     d cmd "$1" ;;
  seed)    [ "$PLATFORM" = win11 ] || fail "seed is win11 only"; win11_seed ;;
  click)   [ "$PLATFORM" = wine ] || fail "click is wine only (use wvm/mvm click)"; wclick "$@" ;;
  *) usage; exit 2 ;;
esac
