#!/usr/bin/env bash
# Bring up a fresh Windows 11 lab with the package installed and the
# autologon test desktop, for tests/apps/d3d12/run.sh.
#
#   tests/apps/d3d12/lab-up.sh <lab-dir> <lab-name> <host-gpu>
#
# Writes <lab-dir>/vmlab.wcl from labs/probe/vmlab.wcl (renamed, host_gpu
# replaced, a NAT NIC added so the evaluation template does not shut down
# after an hour without network), runs `vmlab up`, sets up the autologon
# desktop (tests/guest/Set-TestDesktop.ps1 -Reboot) and installs the bundle
# in out/package/ with scripts/install-into-vm.sh. The VM is "win11".
# Destroy it with (cd <lab-dir> && vmlab destroy).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
[ $# -eq 3 ] || { sed -n '2,13p' "$0" >&2; exit 2; }
mkdir -p "$1"
LABDIR=$(cd "$1" && pwd)
NAME=$2
GPU=$3
VM=win11
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
wait_agent() {
    for _ in $(seq 1 90); do
        in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0
        sleep 10
    done
    note "guest agent did not answer"; return 1
}

sed -e "s/lab \"win3d-host-probe\"/lab \"$NAME\"/" \
    -e "s/host_gpu = \"AMD1\"/host_gpu = \"$GPU\"/" \
    -e "s|^    secure_boot = false|    secure_boot = false\n    nic { nat = true }|" \
    "$REPO/labs/probe/vmlab.wcl" >"$LABDIR/vmlab.wcl"
grep -q "host_gpu = \"$GPU\"" "$LABDIR/vmlab.wcl"
in_lab vmlab validate
note "vmlab up $NAME on $GPU"
in_lab vmlab up
wait_agent
in_lab vmlab exec --timeout 60 "$VM" -- powershell.exe -NoProfile -Command 'New-Item -ItemType Directory -Force C:\win3d | Out-Null'
in_lab vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest"
note "test desktop"
in_lab vmlab exec --timeout 1200 "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
    -File 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot || true
sleep 60; wait_agent
note "install"
"$REPO/scripts/install-into-vm.sh" "$LABDIR" "$VM"
wait_agent
note "ready: $NAME ($LABDIR)"
