#!/bin/bash
# macOS developer build script: build, install (optionally stamp a bumped version).
#
# The component version comes from scripts/version.sh (git describe, e.g.
# 1.21.1-dev.3+2a46400 between releases), so a plain build needs no bump.
#
# Usage:
#   ./mac-dev-build.sh                  — build, install locally (no bump)
#   ./mac-dev-build.sh --patch          — stamp the last release tag's patch + 1
#   ./mac-dev-build.sh --minor          — bump minor (resets patch to 0)
#   ./mac-dev-build.sh --major          — bump major (resets minor + patch to 0)
#   ./mac-dev-build.sh --no-bump        — no bump (the default; kept for old muscle memory)
#   ./mac-dev-build.sh --no-install     — build only (skip install-macos.sh)
#   ./mac-dev-build.sh --no-test        — skip the unit tests that otherwise gate the build
#   ./mac-dev-build.sh --new-release    — bump, build, install, then create a GitHub release

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"   # scripts/
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"          # repo root

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

# ---------------------------------------------------------------------------
# 0. Unit tests (gate the build — fail fast before the long xcodebuild).
#    Same suite CI runs on the macOS runner; --no-test to skip.
# ---------------------------------------------------------------------------
if [ "$RUN_TESTS" = true ]; then
    echo "==> unit tests ..."
    "$SCRIPT_DIR/run-unit-tests.sh" mac
fi

# ---------------------------------------------------------------------------
# 1. Version: git describe, or the last release tag + an explicit bump
#    (scripts/version.sh --base). Nothing is written to the tree.
# ---------------------------------------------------------------------------
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
    export NAVIDROME_VERSION="${MAJOR}.${MINOR}.${PATCH}"   # an explicit bump is what gets stamped
fi
# Resolved here, in the real checkout: the SDK-tree copy below is rsynced without .git, so
# git describe can't run there. Passed to xcodebuild as a build setting, which the
# "Generate Version Header" phase reads; the root header is what install-macos.sh names
# the package and tags a --new-release after.
VERSION="$("$SCRIPT_DIR/version.sh" --header "$ROOT/version_generated.h")"
echo "Version: ${VERSION}"
if [ "$DO_RELEASE" = true ] && [ "$BUMP" = "none" ]; then
    echo "--new-release needs --patch/--minor/--major: ${VERSION} is a dev or already-released version." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 2. Resolve where to build
#
# The Xcode project reaches the SDK through relative paths (../foobar2000,
# ../../pfc), so it only builds from a checkout that has those siblings. When
# the repo lives somewhere else (the normal case), mirror it into the SDK tree
# and build the copy. rsync, not a symlink: Xcode canonicalizes symlinked source
# paths, which breaks the very relative includes we're trying to satisfy.
# ---------------------------------------------------------------------------
# The xcodeproj searches `..` and `../..` for <helpers/...> and <pfc/...>, and the workspace
# points at ../SDK, ../helpers, ../shared and ../../pfc — i.e. the repo has to sit INSIDE the
# SDK's foobar2000/ dir. Test for that exact layout: checking ../foobar2000/helpers instead
# matches a repo that merely sits NEXT to an SDK checkout, where every one of those relative
# paths misses and the build dies on "'helpers/foobar2000+atl.h' file not found".
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

# ---------------------------------------------------------------------------
# 3. Build
#
# Never pipe xcodebuild through `tail`: compile-command echoes scroll the real
# `error:` lines off the top, so a failure shows up as a bare "** BUILD FAILED **"
# with no cause. Full log to a file, filtered summary to the terminal, and the
# error lines re-printed last on failure (that's where the eye lands).
# ---------------------------------------------------------------------------
cd "$BUILD_ROOT"
LOG=/tmp/xcodebuild-dev.log
echo "Building (Release)..."

# Local dev build compiles in the NAVIDROME_DEBUG_LOG tracer (NavidromeDebugLog.h) so
# `make mac-logs` can follow /tmp/foo_navidrome_debug.log live. A --new-release
# build is excluded — it must match the CI (mac-ci-build.sh) binary exactly.
# ${ARR[@]+…} guard: macOS bash 3.2 + `set -u` chokes on a bare empty-array expand.
XCB_EXTRA=()
if [ "$DO_RELEASE" = false ]; then
    XCB_EXTRA=(OTHER_CFLAGS='$(inherited) -DNAVIDROME_DEBUG_LOG=1')
fi

# mac-workspace.xcconfig silences SDK-side warnings (NDEBUG, SDK header/source noise) for every
# target in the workspace; read it before adding anything there. The explicit destination
# avoids "Using the first of multiple matching destinations" and always builds universal.
# MACOSX_DEPLOYMENT_TARGET on the command line applies to every target in the workspace,
# ours and the SDK's own projects alike. Xcode 26+ rejects their hardcoded 11.0 outright
# ("the range of supported deployment target versions is 12.0 to ..."), and the SDK tree is
# re-cloned upstream content we don't get to edit.
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

# ---------------------------------------------------------------------------
# 3. Install (delegates to install-macos.sh, which also packages the .fb2k-component)
# ---------------------------------------------------------------------------
if [ "$DO_INSTALL" = true ]; then
    if [ "$DO_RELEASE" = true ]; then
        "$SCRIPT_DIR/install-macos.sh" --new-release
    else
        "$SCRIPT_DIR/install-macos.sh"
        # Start the debug log clean for this build, marked with the version.
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
