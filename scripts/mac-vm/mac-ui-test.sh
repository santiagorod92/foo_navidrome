#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: mac-ui-test.sh <command>
  smoke [COMPONENT]   deploy, relaunch, browse, play, assert, screenshot
  browser             open the browser in the running foobar2000
  log [N]             last N lines of the guest's debug log
USAGE
}

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
MVM="${MVM:-$(cd "$REPO/.." && pwd)/macos-devbox/mvm}"
[ -x "$MVM" ] || { echo "mvm not found at $MVM"; exit 1; }
GLOG=/tmp/foo_navidrome_debug.log
SHOTS="$REPO/build/ui-test"

FILE_MENU_X=162; FILE_MENU_Y=11
CATEGORY_ROWS=12

say()  { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m  !!\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mFAIL\033[0m %s\n' "$*"; shot_to "$SHOTS/mac-fail-$(date +%H%M%S).png" || true; exit 1; }

g()  { "$MVM" ssh "$@"; }
keys() { "$MVM" key "$@"; sleep 0.3; }
running() { g 'pgrep -x foobar2000 >/dev/null'; }
crash_count() { g 'ls ~/Library/Logs/DiagnosticReports 2>/dev/null | grep -c "^foobar2000" || true'; }
shot_to() {
  local src; src="$("$MVM" shot | tail -1)"
  mkdir -p "$(dirname "$1")"; cp "$src" "$1" && echo "$1"
}

HAVE_LOG=0
mark() { MARK="$(g "wc -c < $GLOG 2>/dev/null || echo 0" | tr -d ' ')"; }
since_mark() { g "tail -c +$(( ${MARK:-0} + 1 )) $GLOG 2>/dev/null" || true; }
expect() {
  [ "$HAVE_LOG" = 1 ] || return 0
  local deadline=$(( SECONDS + $2 ))
  until since_mark | grep -Eq -- "$1"; do
    if [ "$SECONDS" -ge "$deadline" ]; then since_mark | tail -15; fail "$3 — no /$1/ in the log within $2s"; fi
    running || fail "foobar2000 exited while waiting for /$1/ — crashed?"
    sleep 1
  done
  ok "$3"
}

open_browser() {
  g "osascript -e 'tell application \"foobar2000\" to activate'" >/dev/null
  sleep 1
  "$MVM" click "$FILE_MENU_X" "$FILE_MENU_Y"; sleep 1
  "$MVM" type "Open N"; keys enter
  sleep 2
}

newest_component() {
  ls -t "$REPO"/foo_navidrome*.fb2k-component 2>/dev/null | grep -v '_win-' | head -1
}

smoke() {
  local comp="${1:-$(newest_component)}" crashes0
  [ -f "$comp" ] || { echo "no macOS .fb2k-component — make mac-vm-build ARGS=--debug-log, or pass one"; exit 1; }
  say "deploy $(basename "$comp") + relaunch (fresh guest log)"
  crashes0="$(crash_count)"
  g "pkill -x foobar2000 || true; sleep 1; : > $GLOG"
  mark
  "$MVM" deploy "$comp" --launch
  for _ in $(seq 1 30); do running && break; sleep 1; done
  running || fail "foobar2000 didn't start"
  sleep 6
  if g "grep -q ' Env ' $GLOG 2>/dev/null"; then
    HAVE_LOG=1; ok "component loaded (debug log active)"
  else
    warn "no debug log in the guest — release build? log assertions skipped (make mac-vm-build ARGS=--debug-log)"
  fi

  say "File > Open Navidrome Browser"
  mark; open_browser
  expect 'getArtists\.view' 20 "artist list requested"
  expect '200 OK' 20 "artist list loaded"
  sleep 1

  say "expand the first artist (Tab to the list, Down x$(( CATEGORY_ROWS + 1 )), Right)"
  mark
  keys tab
  for _ in $(seq 1 $(( CATEGORY_ROWS + 1 ))); do "$MVM" key down; done
  keys right
  expect 'getArtist\.view' 20 "artist's albums requested"
  expect '200 OK' 20 "albums loaded"
  sleep 1
  shot_to "$SHOTS/mac-browser-$(date +%Y%m%d-%H%M%S).png" >/dev/null && ok "screenshot (browser)"

  say "play its first album (Down x3, Enter = replace playlist + play)"
  mark
  keys down; keys down; keys down; keys enter
  expect 'getAlbum\.view' 20 "album songs requested"
  expect 'decode_initialize' 30 "playback reached the navidrome:// input"
  expect 'decoder ready' 30 "decoder opened the stream"

  if [ "$HAVE_LOG" = 1 ]; then
    sleep 4
    if since_mark | grep -Eq 'Lyrics'; then
      ok "lyrics looked up: $(since_mark | grep -E 'Lyrics' | tail -1 | sed 's/.*\] //')"
    else
      warn "no Lyrics lines — is the \"Navidrome Lyrics\" panel in the layout?"
    fi
  fi

  say "post-checks"
  sleep 2
  running || fail "foobar2000 is not running"
  ok "foobar2000 still running"
  [ "$(crash_count)" = "$crashes0" ] && ok "no new crash report" || fail "new foobar2000 crash report in ~/Library/Logs/DiagnosticReports"
  if [ "$HAVE_LOG" = 1 ]; then
    local errs; errs="$(g "grep -cE '^[0-9:.]+  ERROR ' $GLOG || true")"
    [ "${errs:-0}" = 0 ] && ok "no ERROR lines in the log" \
      || { g "grep -E '^[0-9:.]+  ERROR ' $GLOG | tail -5"; fail "$errs ERROR line(s) in the log"; }
  fi
  shot_to "$SHOTS/mac-smoke-$(date +%Y%m%d-%H%M%S).png" && ok "screenshot (playing)"
  say "mac smoke passed"
}

case "${1:-}" in
  smoke)   shift; smoke "$@" ;;
  browser) open_browser ;;
  log)     g "tail -n ${2:-40} $GLOG" ;;
  *) usage; exit 2 ;;
esac
