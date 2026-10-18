#!/usr/bin/env bash
# Bring up a fresh Windows 11 lab with the autologon desktop and dwm crash
# capture, but no package yet, for the first-boot dwm crash repro.
#
#   docs/evidence/dwmheap/src/lab-prep.sh <lab-dir> <lab-name> <host-gpu> [--pageheap]
#
# Same lab as tests/apps/d3d12/lab-up.sh (labs/probe/vmlab.wcl, NAT NIC),
# then: WER LocalDumps (full dumps of dwm.exe to C:\dumps) and, with
# --pageheap, full PageHeap for dwm.exe (the registry values
# `gflags /p /enable dwm.exe /full` writes). Install afterwards with
# scripts/install-into-vm.sh <lab-dir> win11.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
[ $# -ge 3 ] || { sed -n '2,11p' "$0" >&2; exit 2; }
mkdir -p "$1"
LABDIR=$(cd "$1" && pwd)
NAME=$2 GPU=$3 PH=${4:-}
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
in_lab vmlab exec --timeout 60 "$VM" -- powershell.exe -NoProfile -Command 'New-Item -ItemType Directory -Force C:\win3d, C:\dumps | Out-Null'
in_lab vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest"
note "dwm crash capture"
PS='$k = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\dwm.exe"
New-Item -Path $k -Force | Out-Null
New-ItemProperty -Path $k -Name DumpFolder -PropertyType ExpandString -Value "C:\dumps" -Force | Out-Null
New-ItemProperty -Path $k -Name DumpType -PropertyType DWord -Value 2 -Force | Out-Null
New-ItemProperty -Path $k -Name DumpCount -PropertyType DWord -Value 10 -Force | Out-Null'
if [ "$PH" = --pageheap ]; then
    PS="$PS"'
$i = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\dwm.exe"
New-Item -Path $i -Force | Out-Null
New-ItemProperty -Path $i -Name GlobalFlag -PropertyType DWord -Value 0x02000000 -Force | Out-Null
New-ItemProperty -Path $i -Name PageHeapFlags -PropertyType DWord -Value 3 -Force | Out-Null'
fi
in_lab vmlab exec --timeout 60 "$VM" -- powershell.exe -NoProfile -NonInteractive -Command "$PS"
note "test desktop"
in_lab vmlab exec --timeout 1200 "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
    -File 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot || true
sleep 60; wait_agent
note "ready for install: $NAME ($LABDIR)"
