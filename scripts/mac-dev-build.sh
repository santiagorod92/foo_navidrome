#!/bin/bash
# macOS developer build script: bump version, build, install.
#
# Usage:
#   ./mac-dev-build.sh                  — bump patch, build, install locally
#   ./mac-dev-build.sh --minor          — bump minor (resets patch to 0)
#   ./mac-dev-build.sh --major          — bump major (resets minor + patch to 0)
#   ./mac-dev-build.sh --no-bump        — skip the version bump (just build + install)
#   ./mac-dev-build.sh --no-install     — build only (skip install-macos.sh)
#   ./mac-dev-build.sh --new-release    — bump, build, install, then create a GitHub release

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"   # scripts/
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"          # repo root
VERSION_FILE="${ROOT}/version.txt"

BUMP="patch"
DO_INSTALL=true
DO_RELEASE=false

for arg in "$@"; do
    case "$arg" in
        --major)        BUMP="major"      ;;
        --minor)        BUMP="minor"      ;;
        --patch)        BUMP="patch"      ;;
        --no-bump)      BUMP="none"       ;;
        --no-install)   DO_INSTALL=false  ;;
        --new-release)  DO_RELEASE=true   ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# 1. Bump version.txt
# ---------------------------------------------------------------------------
if [ ! -f "$VERSION_FILE" ]; then
    echo "1.0.0" > "$VERSION_FILE"
fi

CURRENT=$(tr -d '[:space:]' < "$VERSION_FILE")
IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"
MAJOR="${MAJOR:-1}"; MINOR="${MINOR:-0}"; PATCH="${PATCH:-0}"

case "$BUMP" in
    major) MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0 ;;
    minor) MINOR=$((MINOR + 1)); PATCH=0 ;;
    patch) PATCH=$((PATCH + 1)) ;;
    none)  ;;
esac

NEW="${MAJOR}.${MINOR}.${PATCH}"
if [ "$BUMP" != "none" ]; then
    echo "$NEW" > "$VERSION_FILE"
    echo "Version: ${CURRENT} -> ${NEW}"
else
    echo "Version: ${CURRENT} (no bump)"
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
BUILD_ROOT="$ROOT"
if [ ! -f "$ROOT/../foobar2000/helpers/foobar2000+atl.h" ]; then
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

if xcodebuild \
    -workspace foo_navidrome.xcworkspace \
    -scheme foo_navidrome \
    -configuration Release \
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
            "$NEW" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$DBG_LOG" 2>/dev/null || true
    fi
fi

echo ""
echo "Done. Restart foobar2000 to load v${NEW}."
if [ "$DO_INSTALL" = true ] && [ "$DO_RELEASE" = false ]; then
    echo "Then follow component traces with:  make mac-logs"
fi
