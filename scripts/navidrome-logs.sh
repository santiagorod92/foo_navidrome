#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: navidrome-logs.sh [-a] [-n N] [-f PATH] [--no-color]
  (default)    last 40 lines of /tmp/foo_navidrome_debug.log, then follow
  -a           whole file, then follow
  -n N         last N lines, then follow
  -f PATH      a different log file
  --no-color   plain passthrough
USAGE
}

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
    -h|--help)     usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

touch "$LOG" 2>/dev/null || true
echo "==> following $LOG  (Ctrl-C to stop)" >&2

if [ "$FROM_START" = "1" ]; then START=(-n +1); else START=(-n "$LINES"); fi

if [ "$COLOR" = "0" ]; then
  exec tail "${START[@]}" -F "$LOG"
fi

tail "${START[@]}" -F "$LOG" | awk '
  function c(code, s) { return "\033[" code "m" s "\033[0m" }
  /^====/ { print c("1;35", $0); fflush(); next }
  {
    if ($2 ~ /^(INFO|WARN|ERROR|DEBUG)$/) {
      ts = $1; lvl = $2; tag = $3;
      tid = ($4 ~ /^\[t[0-9]+\]$/) ? $4 : "";
      msg = $0; sub(/^[^ ]+[ ]+[^ ]+[ ]+[^ ]+[ ]+/, "", msg);
      if (tid != "") sub(/^\[t[0-9]+\][ ]+/, "", msg);
      lc = (lvl == "ERROR") ? "1;31" : (lvl == "WARN") ? "33" : (lvl == "INFO") ? "32" : "37";
      mc = (lvl == "ERROR") ? "1;31" : (lvl == "WARN") ? "33" : "0";
      printf "%s  %s  %s  %s%s\n", c("2", ts), c(lc, sprintf("%-5s", lvl)),
                                 c("36", sprintf("%-8s", tag)),
                                 (tid != "" ? c("2", tid " ") : ""), c(mc, msg);
    } else {
      print c("2", $0);
    }
    fflush();
  }
'
