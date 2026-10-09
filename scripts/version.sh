#!/bin/bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

archived_describe() {
    local v=""
    [ -f "$ROOT/version.txt" ] && v="$(tr -d '[:space:]' < "$ROOT/version.txt")"
    case "$v" in ''|*'$Format'*) return 1 ;; esac
    echo "$v"
}

describe() {
    git -C "$ROOT" describe --tags --long --match 'v[0-9]*' "$@" 2>/dev/null || archived_describe
}

format() {
    local desc="$1" dirty="" tag count sha
    case "$desc" in *-dirty) dirty=".dirty"; desc="${desc%-dirty}" ;; esac
    case "$desc" in
        *-g*)
            sha="${desc##*-g}";  desc="${desc%-g*}"
            count="${desc##*-}"; tag="${desc%-*}" ;;
        *) tag="$desc"; count=0; sha="" ;;
    esac
    tag="${tag#v}"
    if [ "$count" = "0" ] && [ -z "$dirty" ]; then
        echo "$tag"
    else
        echo "${tag}-dev.${count}+${sha}${dirty}"
    fi
}

if [ "${1:-}" = "--base" ]; then
    desc="$(describe)" || { echo "0.0.0"; exit 0; }
    desc="$(format "$desc")"
    echo "${desc%%-*}"
    exit 0
fi

if [ -n "${NAVIDROME_VERSION:-}" ]; then
    VERSION="$NAVIDROME_VERSION"
elif desc="$(describe --dirty)"; then
    VERSION="$(format "$desc")"
else
    VERSION="0.0.0-unknown"
fi

if [ "${1:-}" = "--header" ]; then
    OUT="${2:?--header needs an output path}"
    CONTENT="$(printf '#pragma once\n#define COMPONENT_VERSION "%s"' "$VERSION")"
    if [ ! -f "$OUT" ] || [ "$(cat "$OUT")" != "$CONTENT" ]; then
        printf '%s\n' "$CONTENT" > "$OUT"
    fi
fi

echo "$VERSION"
