#!/usr/bin/env bash
set -euo pipefail

VMDIR="${VMDIR:-$HOME/.local/share/foo_navidrome-winvm}"
WORK="$VMDIR/_isobuild"
EDITION="${EDITION:-professional}"
LANG_="${LANG_:-en-us}"
mkdir -p "$VMDIR" "$WORK"

for t in aria2c cabextract wimlib-imagex mkisofs curl python3; do
  command -v "$t" >/dev/null || { echo "missing $t — run setup-mac-toolchain.sh"; exit 1; }
done

if [ ! -f "$VMDIR/virtio-win.iso" ]; then
  echo "==> downloading virtio-win.iso"
  curl -L -o "$VMDIR/virtio-win.iso" \
    "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso"
fi

if [ ! -f "$VMDIR/win11-arm64.iso" ]; then
  echo "==> finding latest retail Windows 11 24H2 ARM64 build"
  UUID="$(curl -s "https://api.uupdump.net/listid.php?search=26100" | python3 -c '
import sys,json
items=list(json.load(sys.stdin)["response"]["builds"].values())
arm=[x for x in items if x.get("arch")=="arm64" and x.get("title","").startswith("Windows 11, version 24H2")]
print(arm[0]["uuid"] if arm else "")')"
  [ -n "$UUID" ] || { echo "no retail arm64 build found"; exit 1; }
  echo "    build uuid: $UUID"

  echo "==> fetching uupdump conversion package"
  curl -sL "https://uupdump.net/get.php?id=$UUID&pack=$LANG_&edition=$EDITION" \
    --data "autodl=2&updates=1" -o "$WORK/pkg.zip"
  unzip -oq "$WORK/pkg.zip" -d "$WORK"

  mkdir -p "$WORK/stubbin"; printf '#!/bin/sh\nexit 0\n' > "$WORK/stubbin/chntpw"; chmod +x "$WORK/stubbin/chntpw"

  echo "==> building ISO (downloads ~4GB + converts — 15-30 min)"
  ( cd "$WORK" && PATH="$WORK/stubbin:$PATH" ./uup_download_macos.sh )

  ISO="$(find "$WORK" -maxdepth 1 -iname '*.iso' | head -1)"
  [ -n "$ISO" ] || { echo "conversion produced no ISO — see $WORK"; exit 1; }
  mv "$ISO" "$VMDIR/win11-arm64.iso"
fi

echo "==> ready:"
ls -la "$VMDIR/win11-arm64.iso" "$VMDIR/virtio-win.iso"
