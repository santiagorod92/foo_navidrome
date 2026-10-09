#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUMP="none"
DO_INSTALL=true
DO_RELEASE=false
RUN_TESTS=true

for arg in "$@"; do
    case "$arg" in
        --major)        BUMP="major"      ;;
        --minor)        BUMP="minor"      ;;
        --patch)        BUMP="patch"      ;;
        --no-bump)      BUMP="none"       ;;
        --no-install)   DO_INSTALL=false  ;;
        --no-test)      RUN_TESTS=false   ;;
        --new-release)  DO_RELEASE=true   ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

if [ "$RUN_TESTS" = true ]; then
    echo "==> unit tests ..."
    "$SCRIPT_DIR/run-unit-tests.sh" mac
fi

if [ "$BUMP" != "none" ]; then
    CURRENT="$("$SCRIPT_DIR/version.sh" --base)"
    IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"
    MAJOR="${MAJOR:-0}"; MINOR="${MINOR:-0}"; PATCH="${PATCH:-0}"
    case "$BUMP" in
        major) MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0 ;;
        minor) MINOR=$((MINOR + 1)); PATCH=0 ;;
        patch) PATCH=$((PATCH + 1)) ;;
    esac
    echo "Bump: ${CURRENT} -> ${MAJOR}.${MINOR}.${PATCH}"
    export NAVIDROME_VERSION="${MAJOR}.${MINOR}.${PATCH}"
fi
VERSION="$("$SCRIPT_DIR/version.sh" --header "$ROOT/version_generated.h")"
echo "Version: ${VERSION}"
if [ "$DO_RELEASE" = true ] && [ "$BUMP" = "none" ]; then
    echo "--new-release needs --patch/--minor/--major: ${VERSION} is a dev or already-released version." >&2
    exit 1
fi

BUILD_ROOT="$ROOT"
if [ ! -f "$ROOT/../helpers/foobar2000+atl.h" ]; then
    SDK_TREE="${FOO_NAVIDROME_SDK:-$HOME/.local/share/foo_navidrome-sdk}"
    if [ ! -f "$SDK_TREE/foobar2000/helpers/foobar2000+atl.h" ]; then
        cat >&2 <<EOF
ERROR: foobar2000 SDK not found.

  Checked for siblings of the repo:  $ROOT/../foobar2000, $ROOT/../pfc
  Checked for an SDK tree at:        $SDK_TREE

Fix it either way:
  a) Fetch the SDK into the default tree:
       git clone https://github.com/reupen/foobar2000-sdk-unmodified _sdk
       mkdir -p "$SDK_TREE"
       mv _sdk/foobar2000 _sdk/pfc _sdk/libPPUI "$SDK_TREE"/
     (scripts/win-vm/setup-mac-toolchain.sh does this for you.)
  b) Point FOO_NAVIDROME_SDK at an existing tree laid out as
     <tree>/foobar2000/{SDK,helpers,helpers-mac,shared,...} and <tree>/pfc
EOF
        exit 1
    fi

    BUILD_ROOT="$SDK_TREE/foobar2000/foo_navidrome"
    echo "Building from SDK tree: $BUILD_ROOT"
    mkdir -p "$BUILD_ROOT"
    rsync -a --delete \
        --exclude '.git/' --exclude 'build/' --exclude 'build-win/' \
        --exclude 'build-win-mac/' \
        "$ROOT/" "$BUILD_ROOT/"
fi

cd "$BUILD_ROOT"
LOG=/tmp/xcodebuild-dev.log
echo "Building (Release)..."

XCB_EXTRA=()
if [ "$DO_RELEASE" = false ]; then
    XCB_EXTRA=(OTHER_CFLAGS='$(inherited) -DNAVIDROME_DEBUG_LOG=1')
fi

if xcodebuild \
    -workspace foo_navidrome.xcworkspace \
    -scheme foo_navidrome \
    -configuration Release \
    -destination "generic/platform=macOS" \
    -xcconfig "$SCRIPT_DIR/mac-workspace.xcconfig" \
    MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}" \
    NAVIDROME_VERSION="$VERSION" \
    ${XCB_EXTRA[@]+"${XCB_EXTRA[@]}"} \
    build > "$LOG" 2>&1; then
    grep -E "warning:|\*\* BUILD" "$LOG" | grep -v "iOSSimulator" | tail -n 10 || true
    echo "Build OK. (full log: $LOG)"
else
    echo "" >&2
    echo "BUILD FAILED — full log: $LOG" >&2
    echo "" >&2
    grep -B 3 -E "error:|fatal error:" "$LOG" | grep -v "iOSSimulator" | head -n 40 >&2
    exit 1
fi

if [ "$DO_INSTALL" = true ]; then
    if [ "$DO_RELEASE" = true ]; then
        "$SCRIPT_DIR/install-macos.sh" --new-release
    else
        "$SCRIPT_DIR/install-macos.sh"
        DBG_LOG=/tmp/foo_navidrome_debug.log
        : > "$DBG_LOG" 2>/dev/null || true
        printf '==== build %s installed %s ====\n' \
            "$VERSION" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$DBG_LOG" 2>/dev/null || true
    fi
fi

echo ""
echo "Done. Restart foobar2000 to load v${VERSION}."
if [ "$DO_INSTALL" = true ] && [ "$DO_RELEASE" = false ]; then
    echo "Then follow component traces with:  make mac-logs"
fi
