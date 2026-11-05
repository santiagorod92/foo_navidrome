#!/usr/bin/env bash
set -euo pipefail
version="${1:?usage: release-notes.sh VERSION [CHANGELOG.md]}"
file="${2:-$(dirname "$0")/../CHANGELOG.md}"
notes="$(awk -v v="$version" '
  /^## / { if (found) exit; h = $2; gsub(/^\[|\].*$/, "", h); found = (h == v); next }
  !found { next }
  /^ +[^ ]/ && $0 !~ /^ *([-*]|[0-9]+\.) / && out != "" { sub(/^ +/, " "); out = out $0; next }
  { if (started) print out; out = $0; started = 1 }
  END { if (started) print out }
' "$file" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}')"
if [ -z "$notes" ]; then
  echo "release-notes.sh: no '## [$version]' section in $file: add it to CHANGELOG.md in the release commit" >&2
  exit 1
fi
printf '%s\n' "$notes"
