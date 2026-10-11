#!/bin/bash
usage() {
  cat <<'USAGE'
Usage: scripts/release-package.sh <version>
Release prepare step (.releaserc.json prepareCmd), run on Linux after the parallel builds:
merges the macOS component built by release.yml's build-mac job
($PREBUILT_DIR/mac/foo_navidrome_<version>.fb2k-component) with the Windows DLLs from
build-windows.yml ($PREBUILT_DIR/win/{foo_navidrome.dll,x64/,arm64ec/}) into
foo_navidrome_<version>.fb2k-component in the repo root. Fails when the prebuilt mac component
isn't stamped <version> (the build-time dry run and the real release disagree).
Writes released_version to $GITHUB_OUTPUT when run in Actions.
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail

[ $# -ge 1 ] || { usage >&2; exit 2; }
VERSION="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRE="${PREBUILT_DIR:?PREBUILT_DIR must point at the downloaded build artifacts}"
cd "$ROOT"

ASSET="foo_navidrome_${VERSION}.fb2k-component"
MAC="$PRE/mac/$ASSET"
if [ ! -f "$MAC" ]; then
  echo "release-package: no macOS component stamped $VERSION in $PRE/mac:" >&2
  ls -la "$PRE/mac" >&2 || true
  exit 1
fi
for f in foo_navidrome.dll x64/foo_navidrome.dll arm64ec/foo_navidrome.dll; do
  [ -f "$PRE/win/$f" ] || { echo "release-package: missing Windows $f in $PRE/win" >&2; exit 1; }
done

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "released_version=$VERSION" >> "$GITHUB_OUTPUT"
fi

rm -f "$ASSET"
cp "$MAC" "$ASSET"
(cd "$PRE/win" && zip -q -g "$ROOT/$ASSET" foo_navidrome.dll x64/foo_navidrome.dll arm64ec/foo_navidrome.dll)
unzip -l "$ASSET"
echo "release-package: packaged $ASSET"
