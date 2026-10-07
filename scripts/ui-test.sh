#!/usr/bin/env bash
# ui-test.sh — drive foo_navidrome in the local Wine foobar2000 and check what it did (dev aid,
# Hyprland host). Same mechanism as foo_ui_panels' scripts/ui-test.sh: tools/wclick.c runs inside
# Wine next to foobar2000 and posts input straight to the Navidrome Browser window, so the host's
# real mouse/keyboard are never touched. Assertions read the component's debug log
# (/tmp/foo_navidrome_debug.log — win-build-local.sh builds with NAVIDROME_DEBUG_LOG).
#
#   ui-test.sh smoke                  scripted run: restart, open browser, expand an artist,
#                                     play an album (Enter), assert log + process, screenshot
#   ui-test.sh restart                graceful close, clear the log, relaunch foobar2000
#   ui-test.sh browser                File > Open Navidrome Browser (foobar2000 -command:)
#   ui-test.sh click X Y [top:TITLE] [l|r|dbl|move|hover|close|wheelN|key:VK|type:TEXT|drag:DY|post:MSG:WP:LP|menukey:VK]
#   ui-test.sh key VK [X Y]           post a key to the browser (default: into the tree)
#   ui-test.sh wait REGEX [SECS]      wait for a new log line matching REGEX (since last mark)
#   ui-test.sh shot [FILE.png]        screenshot of the browser window, else the main window (build/ui-test/)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/build/tools"; EXE="$OUT/wclick.exe"
SHOTS="$REPO/build/ui-test"
PREFIX="${WINEPREFIX:-$HOME/.local/share/wineprefixes/foobar2000}"
LOG="${NAVIDROME_DEBUG_LOG_FILE:-/tmp/foo_navidrome_debug.log}"
MARK="$REPO/build/ui-test/.logmark"
XWIN="${XWIN_SDK:-$HOME/.local/share/xwin/sdk}"
# A point inside the browser's tree view (client coords) — the search field and buttons sit
# above/below it at the default 580x660 size. FIRST_ROW_Y is the tree's first row ("All Songs",
# always first): posted keys do nothing until a click has given the tree a selection.
TREE_X=120; TREE_Y=200
FIRST_ROW_Y=43

VK_RETURN=0x0D; VK_END=0x23; VK_HOME=0x24; VK_RIGHT=0x27; VK_DOWN=0x28

say()  { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mFAIL\033[0m %s\n' "$*"; exit 1; }

build_wclick() {
  [ "$EXE" -nt "$REPO/tools/wclick.c" ] && return
  mkdir -p "$OUT"
  clang-cl --target=x86_64-pc-windows-msvc /nologo /O1 /MT /c "$REPO/tools/wclick.c" /Fo"$OUT/wclick.obj" \
    /imsvc"$XWIN/crt/include" /imsvc"$XWIN/sdk/include/ucrt" /imsvc"$XWIN/sdk/include/um" /imsvc"$XWIN/sdk/include/shared" 2>&1 | grep -v msvc-not-found || true
  lld-link /nologo "$OUT/wclick.obj" /libpath:"$XWIN/crt/lib/x86_64" /libpath:"$XWIN/sdk/lib/um/x86_64" \
    /libpath:"$XWIN/sdk/lib/ucrt/x86_64" user32.lib /out:"$EXE"
}
click() { build_wclick; WINEPREFIX="$PREFIX" WINEDEBUG=-all wine "$EXE" "$@"; }
key()   { click "${2:-$TREE_X}" "${3:-$TREE_Y}" "key:$1" >/dev/null; sleep 0.4; }

running() { pgrep -f 'foobar2000.exe' >/dev/null; }

# Log assertions only look at lines written after the last mark, so an earlier step's output
# can't satisfy a later wait.
mark() { mkdir -p "$(dirname "$MARK")"; wc -c < "$LOG" 2>/dev/null > "$MARK" || echo 0 > "$MARK"; }
since_mark() { tail -c +"$(( $(cat "$MARK" 2>/dev/null || echo 0) + 1 ))" "$LOG" 2>/dev/null || true; }
wait_log() {  # REGEX [SECS]
  local re="$1" secs="${2:-20}" deadline=$(( SECONDS + ${2:-20} ))
  until since_mark | grep -Eq -- "$re"; do
    [ "$SECONDS" -lt "$deadline" ] || return 1
    running || fail "foobar2000 exited while waiting for /$re/ — crashed?"
    sleep 0.5
  done
}
expect() {  # REGEX SECS DESCRIPTION
  wait_log "$1" "$2" && ok "$3" || { since_mark | tail -15; fail "$3 — no /$1/ in the log within $2s"; }
}

# Largest window whose title matches; the browser is a top-level "Navidrome Browser" window.
geom() {
  hyprctl clients -j | python3 -c "import json,sys
t = sys.argv[1]
cs = [c for c in json.load(sys.stdin) if c['class'].lower() == 'foobar2000.exe' and t in c['title']]
if not cs: sys.exit(1)
c = max(cs, key=lambda c: c['size'][0] * c['size'][1])
print('%d,%d %dx%d' % (c['at'][0], c['at'][1], c['size'][0], c['size'][1]))" "${1-Navidrome Browser}"
}
shot() {
  local f="${1:-$SHOTS/browser-$(date +%H%M%S).png}" g
  mkdir -p "$(dirname "$f")"
  # The browser closes itself after Enter/Play — fall back to the main window then.
  g="$(geom || geom '')" || { echo "no foobar2000 window to capture"; return 1; }
  grim -g "$g" "$f" && echo "$f"
}

restart() {
  if running; then
    foobar2000 -exit >/dev/null 2>&1 || true
    for _ in $(seq 1 20); do running || break; sleep 0.5; done
    pkill -f 'foobar2000.exe' || true; sleep 1
  fi
  : > "$LOG" 2>/dev/null || true
  mark
  nohup foobar2000 >/dev/null 2>&1 &
  for _ in $(seq 1 40); do running && break; sleep 0.5; done
  running || fail "foobar2000 didn't start"
  sleep 5
}

open_browser() {
  foobar2000 "-command:Open Navidrome Browser" >/dev/null 2>&1 &
  for _ in $(seq 1 30); do geom >/dev/null 2>&1 && return 0; sleep 0.5; done
  fail "the Navidrome Browser window didn't open"
}

smoke() {
  say "restart foobar2000 (fresh log)"
  restart
  grep -q 'Env' "$LOG" 2>/dev/null || echo "  (no Env line yet — is this a NAVIDROME_DEBUG_LOG build? make win-build-launch)"

  say "open the browser"
  mark; open_browser
  expect 'getArtists\.view' 15 "artist list requested"
  expect '200 OK' 20 "artist list loaded"
  sleep 1

  say "expand the last artist (click first row, End, Right)"
  mark; click 60 $FIRST_ROW_Y l >/dev/null; sleep 0.5; key $VK_END; key $VK_RIGHT
  expect 'getArtist\.view' 15 "artist's albums requested"
  expect '200 OK' 20 "albums loaded"
  sleep 1

  # Children: "Top Songs", "Similar Artists", then the albums.
  say "play its first album (Down x3, Enter = replace playlist + play)"
  mark; key $VK_DOWN; key $VK_DOWN; key $VK_DOWN; key $VK_RETURN
  expect 'queueNodes: .*play=1' 15 "Enter queued the selection for playback"
  expect 'getAlbum\.view' 15 "album songs requested"
  expect 'decode_initialize' 20 "playback reached the navidrome:// input"
  expect 'decoder ready' 30 "decoder opened the stream"

  say "post-checks"
  sleep 2
  running || fail "foobar2000 is not running"
  ok "foobar2000 still running"
  local errs; errs="$(grep -cE '^[0-9:.]+  ERROR ' "$LOG" 2>/dev/null || true)"
  [ "${errs:-0}" = 0 ] && ok "no ERROR lines in the log" \
    || { grep -E '^[0-9:.]+  ERROR ' "$LOG" | tail -5; fail "$errs ERROR line(s) in the log"; }
  shot "$SHOTS/smoke-$(date +%Y%m%d-%H%M%S).png" && ok "screenshot"
  say "smoke passed"
}

case "${1:-}" in
  smoke)   smoke ;;
  restart) restart ;;
  browser) open_browser ;;
  click)   shift; click "$@" ;;
  key)     shift; key "$@" ;;
  wait)    shift; wait_log "$@" ;;
  mark)    mark ;;
  shot)    shift; shot "$@" ;;
  *) sed -n '2,15p' "$0"; exit 2 ;;
esac
