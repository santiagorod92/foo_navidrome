#!/usr/bin/env bash
# navidrome-logs.sh — follow the local dev-loop debug log with colour.
# Platform-neutral: `make win-logs` (Wine) and `make mac-logs` both call it.
#
# The component, built by win-build-local.sh or mac-dev-build.sh (both define
# NAVIDROME_DEBUG_LOG), writes structured traces via NavidromeDebugLog.h to
#   /tmp/foo_navidrome_debug.log        (Wine writes it as Z:\tmp\...)
# Each line is:  HH:MM:SS.mmm  LEVEL  TAG       message
# The dev-build scripts truncate the file per build and drop a
# "==== build <ver> installed ... ====" marker at the top.
#
# Usage:
#   ./navidrome-logs.sh            # last 40 lines, then follow
#   ./navidrome-logs.sh -a         # whole file, then follow
#   ./navidrome-logs.sh -n 100     # last 100 lines, then follow
#   ./navidrome-logs.sh -f PATH    # a different log file
#   ./navidrome-logs.sh --no-color # plain passthrough

set -euo pipefail

LOG="${NAVIDROME_DEBUG_LOG_FILE:-/tmp/foo_navidrome_debug.log}"
LINES=40
FROM_START=0
COLOR=1

while [ $# -gt 0 ]; do
  case "$1" in
    -a|--all)      FROM_START=1; shift ;;
    -n)            LINES="${2:?}"; shift 2 ;;
    -f|--file)     LOG="${2:?}"; shift 2 ;;
    --no-color)    COLOR=0; shift ;;
    -h|--help)     grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

touch "$LOG" 2>/dev/null || true
echo "==> following $LOG  (Ctrl-C to stop)" >&2

if [ "$FROM_START" = "1" ]; then START=(-n +1); else START=(-n "$LINES"); fi

if [ "$COLOR" = "0" ]; then
  exec tail "${START[@]}" -F "$LOG"
fi

# Colourise: dim timestamp, level-coloured LEVEL, cyan TAG, ERROR body in red.
# Session/build banners (==== … ====) in bold magenta. Anything that doesn't
# match the 4-column shape (stray console lines, tracebacks) prints dimmed.
tail "${START[@]}" -F "$LOG" | awk '
  function c(code, s) { return "\033[" code "m" s "\033[0m" }
  /^====/ { print c("1;35", $0); fflush(); next }
  {
    if ($2 ~ /^(INFO|WARN|ERROR|DEBUG)$/) {
      ts = $1; lvl = $2; tag = $3;
      msg = $0; sub(/^[^ ]+[ ]+[^ ]+[ ]+[^ ]+[ ]+/, "", msg);
      lc = (lvl == "ERROR") ? "1;31" : (lvl == "WARN") ? "33" : (lvl == "INFO") ? "32" : "37";
      mc = (lvl == "ERROR") ? "1;31" : (lvl == "WARN") ? "33" : "0";
      printf "%s  %s  %s  %s\n", c("2", ts), c(lc, sprintf("%-5s", lvl)),
                                 c("36", sprintf("%-8s", tag)), c(mc, msg);
    } else {
      print c("2", $0);
    }
    fflush();
  }
'
