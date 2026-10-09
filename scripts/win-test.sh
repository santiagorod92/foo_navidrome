#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: win-test.sh [--ref <branch|tag|sha>] [--launch] [--no-wait]
Builds the x64 DLL on a GitHub runner (build-windows.yml) and installs it into Wine foobar2000.
  --ref REF    build this pushed ref instead of the current branch
  --launch     relaunch foobar2000 after installing
  --no-wait    dispatch the build and exit
Requires: gh (authenticated), git, unzip.
USAGE
}

set -euo pipefail

WORKFLOW="build-windows.yml"
ARTIFACT="windows-component"
COMPONENT_DIR="$HOME/.foobar2000/profile/user-components-x64/foo_navidrome"
FOOBAR_LAUNCHER="foobar2000"

REF=""
LAUNCH=0
WAIT=1

while [ $# -gt 0 ]; do
  case "$1" in
    --ref)      REF="${2:-}"; shift 2 ;;
    --launch)   LAUNCH=1; shift ;;
    --no-wait)  WAIT=0; shift ;;
    -h|--help)  usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

cd "$(dirname "$0")/.."

command -v gh   >/dev/null || { echo "ERROR: gh not found"; exit 1; }
command -v unzip >/dev/null || { echo "ERROR: unzip not found"; exit 1; }

if [ -z "$REF" ]; then
  REF="$(git rev-parse --abbrev-ref HEAD)"
fi
echo "==> ref: $REF"

if git rev-parse --verify --quiet "origin/$REF" >/dev/null; then
  AHEAD="$(git rev-list --count "origin/$REF..$REF" 2>/dev/null || echo 0)"
  if [ "$AHEAD" != "0" ]; then
    echo "WARNING: local '$REF' is $AHEAD commit(s) ahead of origin/$REF."
    echo "         The runner builds what's on GitHub — push first or the build"
    echo "         won't include your latest changes."
  fi
else
  echo "WARNING: origin/$REF not found. Push the branch before building, or the"
  echo "         dispatch will fail."
fi

PREV_ID="$(gh run list --workflow "$WORKFLOW" --json databaseId \
             --jq '.[0].databaseId // 0' 2>/dev/null || echo 0)"

echo "==> dispatching $WORKFLOW on $REF ..."
gh workflow run "$WORKFLOW" --ref "$REF"

if [ "$WAIT" = "0" ]; then
  echo "==> dispatched (--no-wait). Watch it with: gh run watch --workflow $WORKFLOW"
  exit 0
fi

echo -n "==> waiting for the run to start"
RUN_ID=""
for _ in $(seq 1 30); do
  CUR_ID="$(gh run list --workflow "$WORKFLOW" --json databaseId \
              --jq '.[0].databaseId // 0' 2>/dev/null || echo 0)"
  if [ "$CUR_ID" != "0" ] && [ "$CUR_ID" != "$PREV_ID" ]; then
    RUN_ID="$CUR_ID"; break
  fi
  echo -n "."
  sleep 2
done
echo
if [ -z "$RUN_ID" ]; then
  echo "ERROR: couldn't find the dispatched run. Check: gh run list --workflow $WORKFLOW"
  exit 1
fi
echo "==> run id: $RUN_ID  ($(gh run view "$RUN_ID" --json url --jq .url))"

set +e
gh run watch "$RUN_ID" --exit-status
WATCH_RC=$?
set -e
if [ "$WATCH_RC" != "0" ]; then
  echo "ERROR: build failed. Logs: gh run view $RUN_ID --log-failed"
  echo "       (the msbuild log is also uploaded as the 'msbuild-log' artifact)"
  exit "$WATCH_RC"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "==> downloading artifact '$ARTIFACT' ..."
gh run download "$RUN_ID" --name "$ARTIFACT" --dir "$TMP"

DLL="$(find "$TMP" -path '*/x64/foo_navidrome.dll' -type f | head -n1)"
if [ -z "$DLL" ]; then
  DLL="$(find "$TMP" -name foo_navidrome.dll -type f | head -n1)"
fi
if [ -z "$DLL" ]; then
  echo "ERROR: foo_navidrome.dll not found in the artifact:" >&2
  find "$TMP" -type f >&2
  exit 1
fi

mkdir -p "$COMPONENT_DIR"
cp -f "$DLL" "$COMPONENT_DIR/foo_navidrome.dll"
echo "==> installed: $COMPONENT_DIR/foo_navidrome.dll"

if [ "$LAUNCH" = "1" ]; then
  echo "==> relaunching foobar2000 ..."
  pkill -f 'foobar2000.exe' 2>/dev/null || true
  sleep 1
  nohup "$FOOBAR_LAUNCHER" >/dev/null 2>&1 &
  echo "==> launched. Check Preferences › Components and View › Console."
else
  echo "==> done. Restart foobar2000 to load the new component:"
  echo "      $FOOBAR_LAUNCHER"
fi
