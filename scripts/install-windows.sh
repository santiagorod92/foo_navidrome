#!/usr/bin/env bash

set -euo pipefail

NEW_RELEASE=false
for arg in "$@"; do
    case "$arg" in
        --new-release) NEW_RELEASE=true ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

COMPONENT_NAME="foo_navidrome"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILT_DLL="${BUILT_DLL:-${ROOT}/build-win/${COMPONENT_NAME}.dll}"
FB2K_COMPONENTS="${FB2K_COMPONENTS:-${HOME}/.foobar2000/profile/user-components-x64}"

if [ ! -f "$BUILT_DLL" ]; then
    echo "ERROR: ${BUILT_DLL} not found."
    echo "Build it first:  ./win-build-local.sh"
    exit 1
fi

DEST="${FB2K_COMPONENTS}/${COMPONENT_NAME}"
echo "Installing: $BUILT_DLL"
echo "       To:  $DEST"
mkdir -p "$DEST"
cp -f "$BUILT_DLL" "$DEST/${COMPONENT_NAME}.dll"

VERSION=""
VERSION_HEADER="${ROOT}/version_generated.h"
if [ -f "$VERSION_HEADER" ]; then
    VERSION=$(grep 'COMPONENT_VERSION' "$VERSION_HEADER" | sed 's/.*"\(.*\)".*/\1/')
fi
VERSION_SUFFIX="${VERSION:+_${VERSION}}"

OUTPUT="${ROOT}/${COMPONENT_NAME}${VERSION_SUFFIX}_win-x64.fb2k-component"
echo "Packaging:  $OUTPUT"

TMPDIR_PKG=$(mktemp -d)
trap 'rm -rf "$TMPDIR_PKG"' EXIT

mkdir -p "$TMPDIR_PKG/x64"
cp -f "$BUILT_DLL" "$TMPDIR_PKG/x64/${COMPONENT_NAME}.dll"
rm -f "$OUTPUT"
( cd "$TMPDIR_PKG" && zip -rq "$OUTPUT" x64 )

echo ""
echo "Local install:  ${DEST}/${COMPONENT_NAME}.dll"
echo "Distributable:  $OUTPUT"
echo ""
echo "Restart foobar2000 to load the component."
echo "Preferences > Tools > Navidrome — enter your server URL and credentials."

if [ "$NEW_RELEASE" = true ]; then
    if [ -z "$VERSION" ]; then
        echo "ERROR: Cannot create release — version_generated.h not found or empty."
        exit 1
    fi
    TAG="v${VERSION}"
    REPO="santiagorod92/${COMPONENT_NAME}"
    echo ""
    echo "Creating GitHub release ${TAG} on ${REPO}…"
    gh release create "$TAG" "$OUTPUT" \
        --repo "$REPO" \
        --title "$TAG" \
        --notes "See [README](https://github.com/${REPO}#readme) for installation instructions." \
        || gh release upload "$TAG" "$OUTPUT" --repo "$REPO" --clobber
    echo "Release published: https://github.com/${REPO}/releases/tag/${TAG}"
fi
