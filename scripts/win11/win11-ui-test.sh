#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: win11-ui-test.sh <command>
  seed               copy foo_navidrome's settings from the Wine profile into the guest
  smoke [DLL]        deploy (default build-win/foo_navidrome.dll), relaunch, browse, play, assert, screenshot
  browser            open the browser in the running foobar2000
  prefs [PAGE]       Preferences on one of our pages, screenshot
                     (main | audiomuse | libraries | radio | media | components)
  log [N]            last N lines of the debug log
USAGE
}

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WVM="${WVM:-$(cd "$REPO/.." && pwd)/windows-devbox/wvm}"
[ -x "$WVM" ] || { echo "wvm not found at $WVM (clone windows-devbox next to this repo)"; exit 1; }
SHOTS="$REPO/build/ui-test"
WINE_CFG="${WINE_CFG:-$HOME/.foobar2000/profile/config.sqlite}"
GUEST_PROFILE='AppData/Roaming/foobar2000-v2'
GUID_PREFIX='A1B2C3D4-1111-2222-AABB-CCDDEEFF'

FIRST_ROW_X=60; FIRST_ROW_Y=43

say()  { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m  !!\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mFAIL\033[0m %s\n' "$*"; shot "$SHOTS/win11-fail-$(date +%H%M%S).png" || true; exit 1; }

LOG="$("$WVM" shared)/tmp/foo_navidrome_debug.log"
gps() { "$WVM" ps "$@"; }
running() { gps 'if (Get-Process foobar2000 -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }' 2>/dev/null; }
crash_count() { gps "(Get-ChildItem \"\$env:APPDATA\\foobar2000-v2\\crash reports\" -Filter *.txt -ErrorAction SilentlyContinue | Measure-Object).Count" | tr -d '\r '; }
shot() { mkdir -p "$(dirname "$1")"; "$WVM" shot "$1" >/dev/null && echo "$1"; }

mark() { MARK="$(wc -c < "$LOG" 2>/dev/null || echo 0)"; }
since_mark() { tail -c +"$(( ${MARK:-0} + 1 ))" "$LOG" 2>/dev/null || true; }
expect() {
  local deadline=$(( SECONDS + $2 ))
  until since_mark | grep -Eq -- "$1"; do
    if [ "$SECONDS" -ge "$deadline" ]; then since_mark | tail -15; fail "$3 — no /$1/ in the log within $2s"; fi
    running || fail "foobar2000 exited while waiting for /$1/ — crashed?"
    sleep 1
  done
  ok "$3"
}

window() { "$WVM" window "$1" 2>/dev/null | awk 'NF >= 5 { print $1, $2, $3, $4, $5; exit }'; }
wait_window() {
  local deadline=$(( SECONDS + ${2:-20} )) w
  until w="$(window "$1")" && [ -n "$w" ]; do
    [ "$SECONDS" -lt "$deadline" ] || return 1
    sleep 1
  done
  echo "$w"
}

with_guest_config() {
  local tmp; tmp="$(mktemp -d)"
  "$WVM" fb2k stop
  if ! "$WVM" pull "$GUEST_PROFILE/config.sqlite" "$tmp" 2>/dev/null; then
    say "no guest config yet — starting foobar2000 once to create it"
    "$WVM" fb2k start; sleep 8; "$WVM" fb2k stop
    "$WVM" pull "$GUEST_PROFILE/config.sqlite" "$tmp" || fail "the guest foobar2000 wrote no config.sqlite"
  fi
  sqlite3 "$tmp/config.sqlite" "ATTACH '$WINE_CFG' AS wine; $1"
  "$WVM" push "$tmp/config.sqlite" "$GUEST_PROFILE"
  rm -rf "$tmp"
}

seed() {
  [ -f "$WINE_CFG" ] || fail "no Wine profile at $WINE_CFG (WINE_CFG=...)"
  local n
  n="$(sqlite3 "$WINE_CFG" "SELECT COUNT(*) FROM configStrings WHERE name LIKE 'cfg_var.$GUID_PREFIX%'")"
  [ "$n" -gt 0 ] || fail "the Wine profile has no foo_navidrome settings to copy"
  say "copying $n foo_navidrome settings from the Wine profile into the guest"
  with_guest_config "
    INSERT OR REPLACE INTO main.configStrings SELECT * FROM wine.configStrings WHERE name LIKE 'cfg_var.$GUID_PREFIX%';
    INSERT OR REPLACE INTO main.configInts    SELECT * FROM wine.configInts    WHERE name LIKE 'cfg_var.$GUID_PREFIX%';
    INSERT OR REPLACE INTO main.configBlobs   SELECT * FROM wine.configBlobs   WHERE name LIKE 'cfg_var.$GUID_PREFIX%';"
  ok "settings copied"
}

restart() {
  "$WVM" fb2k stop
  mkdir -p "$(dirname "$LOG")"; : > "$LOG"
  mark
  "$WVM" fb2k start
  for _ in $(seq 1 30); do running && break; sleep 1; done
  running || fail "foobar2000 didn't start"
  sleep 6
}

open_browser() {
  "$WVM" fb2k cmd "Open Navidrome Browser"
  BROWSER="$(wait_window "Navidrome Browser" 30)" || fail "the Navidrome Browser window didn't open"
}

click_client() {
  local x y w h dpi
  read -r x y w h dpi <<<"$BROWSER"
  "$WVM" click $(( x + $1 * dpi / 96 )) $(( y + $2 * dpi / 96 ))
}
key() { "$WVM" key "$@"; sleep 0.4; }

smoke() {
  local dll="${1:-$REPO/build-win/foo_navidrome.dll}" crashes
  [ -f "$dll" ] || fail "no $dll — build it first (make win-build)"
  say "deploy $(basename "$dll")"
  "$WVM" deploy "$dll"
  crashes="$(crash_count)"

  say "restart foobar2000 (fresh log)"
  restart
  grep -q 'Env' "$LOG" 2>/dev/null && ok "debug log is live ($(wc -l < "$LOG") lines)" \
    || fail "nothing in $LOG — not a NAVIDROME_DEBUG_LOG build, or Z: isn't mapped in the session"

  say "open the browser"
  mark; open_browser
  ok "browser window: $BROWSER"
  expect 'getArtists\.view' 15 "artist list requested"
  expect '200 OK' 20 "artist list loaded"
  sleep 1

  say "expand the last artist (click first row, End, Right)"
  mark; click_client "$FIRST_ROW_X" "$FIRST_ROW_Y"; sleep 0.5; key end; key right
  expect 'getArtist\.view' 15 "artist's albums requested"
  expect '200 OK' 20 "albums loaded"
  sleep 1

  say "play its first album (Down x3, Enter = replace playlist + play)"
  mark; key down; key down; key down; key enter
  expect 'queueNodes: .*play=1' 15 "Enter queued the selection for playback"
  expect 'getAlbum\.view' 15 "album songs requested"
  expect 'decode_initialize' 20 "playback reached the navidrome:// input"
  expect 'decoder ready' 30 "decoder opened the stream"

  say "post-checks"
  sleep 2
  running || fail "foobar2000 is not running"
  ok "foobar2000 still running"
  [ "$(crash_count)" = "$crashes" ] && ok "no new crash report" || fail "foobar2000 wrote a crash report"
  local errs; errs="$(grep -cE '^[0-9:.]+  ERROR ' "$LOG" 2>/dev/null || true)"
  [ "${errs:-0}" = 0 ] && ok "no ERROR lines in the log" \
    || { grep -E '^[0-9:.]+  ERROR ' "$LOG" | tail -5; fail "$errs ERROR line(s) in the log"; }
  shot "$SHOTS/win11-smoke-$(date +%Y%m%d-%H%M%S).png" && ok "screenshot"
  say "smoke passed"
}

prefs() {
  local guid
  case "${1:-main}" in
    main) guid=${GUID_PREFIX}0105 ;; audiomuse) guid=${GUID_PREFIX}0405 ;; libraries) guid=${GUID_PREFIX}0112 ;;
    radio) guid=${GUID_PREFIX}010F ;; media) guid=${GUID_PREFIX}0109 ;;
    components) guid=0E966267-7DFB-433B-A07C-3F8CDD31A258 ;;
    *) fail "unknown page '$1' (main | audiomuse | libraries | radio | media | components)" ;;
  esac
  with_guest_config "INSERT OR REPLACE INTO main.configStrings VALUES ('preferences.lastOpenPage', '$guid');"
  "$WVM" fb2k start '/config'
  wait_window "Preferences" 30 >/dev/null || fail "Preferences didn't open"
  sleep 2
  shot "$SHOTS/win11-prefs-${1:-main}-$(date +%H%M%S).png"
}

case "${1:-}" in
  seed)    seed ;;
  smoke)   shift; smoke "$@" ;;
  browser) open_browser; echo "$BROWSER" ;;
  prefs)   shift; prefs "$@" ;;
  log)     tail -n "${2:-50}" "$LOG" ;;
  *) usage; exit 2 ;;
esac
