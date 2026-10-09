#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SSH_PORT="${SSH_PORT:-2222}"
VMDIR="${VMDIR:-$HOME/.local/share/foo_navidrome-winvm}"
COMMON=(-i "$VMDIR/id_vm" -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)
SSH_OPTS=(-p "$SSH_PORT" "${COMMON[@]}")
SCP_OPTS=(-P "$SSH_PORT" "${COMMON[@]}")
GUEST_DIR='C:\Users\tester\AppData\Roaming\foobar2000-v2\user-components-arm64ec\foo_navidrome'
LAUNCH=0; [ "${1:-}" = "--launch" ] && LAUNCH=1

echo "==> building x64 DLL ..."
"$HERE/build-mac.sh"
DLL="$REPO/build-win-mac/foo_navidrome.dll"
[ -f "$DLL" ] || { echo "build produced no DLL"; exit 1; }

echo "==> waiting for guest SSH on localhost:$SSH_PORT ..."
for i in $(seq 1 60); do
  ssh "${SSH_OPTS[@]}" tester@localhost "echo ok" >/dev/null 2>&1 && break
  sleep 5
  [ "$i" = 60 ] && { echo "guest SSH not reachable — is win-vm.sh run booted + provisioned?"; exit 1; }
done

echo "==> deploying component ..."
ssh "${SSH_OPTS[@]}" tester@localhost "cmd /c \"mkdir \"$GUEST_DIR\" 2>nul & taskkill /IM foobar2000.exe /F 2>nul & exit /b 0\""
scp "${SCP_OPTS[@]}" "$DLL" "tester@localhost:AppData/Roaming/foobar2000-v2/user-components-arm64ec/foo_navidrome/foo_navidrome.dll"

if [ "$LAUNCH" = 1 ]; then
  echo "==> relaunching foobar2000 ..."
  ssh "${SSH_OPTS[@]}" tester@localhost \
    "cmd /c start \"\" \"%ProgramFiles%\\foobar2000\\foobar2000.exe\"" || true
fi
echo "==> done. In the guest: open the Navidrome browser and right-click a row."
