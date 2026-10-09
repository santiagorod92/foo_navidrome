#!/bin/bash

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <version>" >&2
    exit 1
fi

VERSION="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPONENT_NAME="foo_navidrome"

cd "$ROOT"

export NAVIDROME_VERSION="$VERSION"
echo "mac-ci-build: version = $VERSION"

if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "released_version=$VERSION" >> "$GITHUB_OUTPUT"
    echo "mac-ci-build: exported released_version=$VERSION to GITHUB_OUTPUT"
fi

echo "mac-ci-build: xcodebuild Release ..."
LOG=/tmp/xcodebuild.log
set +e
EXTRA=()
if [ -n "${MAC_EXTRA_CFLAGS:-}" ]; then
    EXTRA=(OTHER_CFLAGS="\$(inherited) $MAC_EXTRA_CFLAGS")
    echo "mac-ci-build: extra CFLAGS: $MAC_EXTRA_CFLAGS"
fi
xcodebuild \
    -workspace foo_navidrome.xcworkspace \
    -scheme foo_navidrome \
    -configuration Release \
    -derivedDataPath build/derived \
    -destination "generic/platform=macOS" \
    -xcconfig "$SCRIPT_DIR/mac-workspace.xcconfig" \
    MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}" \
    NAVIDROME_VERSION="$VERSION" \
    ${EXTRA[@]+"${EXTRA[@]}"} \
    build > "$LOG" 2>&1
XCB_RC=$?
set -e

grep -E "error:|warning:|note:|\\*\\* BUILD|ld:|fatal:|FAILED" "$LOG" || true

if [ $XCB_RC -ne 0 ]; then
    echo ""
    echo "===== FULL xcodebuild log (rc=$XCB_RC) ====="
    cat "$LOG"
    echo ""
    echo "===== ERROR LINES (rc=$XCB_RC) ====="
    grep -B 3 -E "error:|fatal error:" "$LOG" || echo "(no error: lines found — search the full log above)"
    echo ""
    echo "===== END (xcodebuild rc=$XCB_RC) ====="
    exit $XCB_RC
fi

COMPONENT="build/derived/Build/Products/Release/${COMPONENT_NAME}.component"
if [ ! -d "$COMPONENT" ]; then
    echo "ERROR: built bundle not found at $COMPONENT" >&2
    exit 1
fi
echo "mac-ci-build: built $COMPONENT"

codesign --sign - --force --deep "$COMPONENT"

OUTPUT="${ROOT}/${COMPONENT_NAME}_${VERSION}.fb2k-component"
TMPDIR_PKG=$(mktemp -d)
trap 'rm -rf "$TMPDIR_PKG"' EXIT

mkdir -p "$TMPDIR_PKG/mac"
cp -Rf "$COMPONENT" "$TMPDIR_PKG/mac/"

ditto --noqtn -ck --norsrc "$TMPDIR_PKG" "$OUTPUT"

echo "mac-ci-build: packaged $OUTPUT"
