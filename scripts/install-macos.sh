#!/bin/bash

set -euo pipefail

NEW_RELEASE=false
for arg in "$@"; do
    case "$arg" in
        --new-release) NEW_RELEASE=true ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

COMPONENT_NAME="foo_navidrome"
BUILD_DIR="${HOME}/Library/Developer/Xcode/DerivedData"
FB2K_COMPONENTS="${HOME}/Library/foobar2000-v2/user-components"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

find_component() {
    local pattern="$1"
    find "$BUILD_DIR" -path "*/Products/${pattern}/${COMPONENT_NAME}.component" \
        -not -path "*/Index.noindex/*" 2>/dev/null \
        | xargs -I{} stat -f "%m %N" {} 2>/dev/null \
        | sort -rn | head -1 | awk '{print $2}'
}

COMPONENT=$(find_component "Release")
if [ -z "$COMPONENT" ]; then
    echo "No Release build found, falling back to Debug."
    COMPONENT=$(find_component "Debug")
fi

if [ -z "$COMPONENT" ]; then
    echo "ERROR: Could not find ${COMPONENT_NAME}.component in DerivedData."
    echo "Please build the project first: open foo_navidrome.xcworkspace and press Cmd+B"
    exit 1
fi

DEST="${FB2K_COMPONENTS}/${COMPONENT_NAME}"
echo "Installing: $COMPONENT"
echo "       To:  $DEST"
mkdir -p "$DEST"
cp -Rf "$COMPONENT" "$DEST/"

INSTALLED="${DEST}/${COMPONENT_NAME}.component"
echo "Signing:    $INSTALLED"
codesign --sign - --force --deep "$INSTALLED"

VERSION=""
VERSION_HEADER="${ROOT}/version_generated.h"
if [ -f "$VERSION_HEADER" ]; then
    VERSION=$(grep 'COMPONENT_VERSION' "$VERSION_HEADER" | sed 's/.*"\(.*\)".*/\1/')
fi
VERSION_SUFFIX="${VERSION:+_${VERSION}}"

OUTPUT="${ROOT}/${COMPONENT_NAME}${VERSION_SUFFIX}.fb2k-component"
echo "Packaging:  $OUTPUT"

TMPDIR_PKG=$(mktemp -d)
trap 'rm -rf "$TMPDIR_PKG"' EXIT

mkdir -p "$TMPDIR_PKG/mac"
cp -Rf "$INSTALLED" "$TMPDIR_PKG/mac/"

ditto --noqtn -ck --norsrc \
    "$TMPDIR_PKG" "$OUTPUT"

echo ""
echo "Local install:  $INSTALLED"
echo "Distributable:  $OUTPUT"
echo ""
echo "Restart foobar2000 to load the component."
echo "Preferences > Tools > Navidrome — enter your server URL and credentials."
echo ""
echo "To distribute: share ${COMPONENT_NAME}${VERSION_SUFFIX}.fb2k-component"
echo "Users can install it by dragging it onto foobar2000 or double-clicking it."

if [ "$NEW_RELEASE" = true ]; then
    if [ -z "$VERSION" ]; then
        echo "ERROR: Cannot create release — version_generated.h not found or empty."
        exit 1
    fi

    TAG="v${VERSION}"
    REPO="santiagorod92/${COMPONENT_NAME}"

    echo ""
    echo "Creating GitHub release ${TAG} on ${REPO}…"

    GH_TOKEN="" gh release create "$TAG" "$OUTPUT" \
        --repo "$REPO" \
        --title "$TAG" \
        --notes "See [README](https://github.com/${REPO}#readme) for installation instructions."

    echo "Release published: https://github.com/${REPO}/releases/tag/${TAG}"
fi
