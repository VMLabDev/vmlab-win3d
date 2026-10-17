#!/usr/bin/env bash
# Run the package acceptance sequence against a running vmlab Windows VM and
# write the evidence into a directory:
#
#   scripts/test-package-in-lab.sh <lab-dir> <vm> <evidence-dir> [--bundle DIR]
#
#   00 test desktop (autologon user for the probes), then the baseline
#   01 install, 02 state after install, 03 probes
#   04 install again (idempotence), 05 state after the rerun
#   06 uninstall, 07 state after uninstall, compared with the baseline
#   08 install after the uninstall, 09 state after it
#
# Needs tests/guest/ and out/probes/ (build/probes/build.sh). Set VMLAB to
# use another vmlab command. Each step's output goes to <evidence-dir>; a
# failing step stops the run.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
VMLAB=${VMLAB:-vmlab}
LABDIR=$(cd "$1" && pwd)
VM=$2
mkdir -p "$3"; EVID=$(cd "$3" && pwd)
shift 3
BUNDLE_ARGS=("$@")
cd "$LABDIR"

say() { printf '\n[%s] == %s\n' "$(date +%H:%M:%S)" "$*"; }
ps() { "$VMLAB" exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
wait_agent() {
    for _ in $(seq 1 90); do
        "$VMLAB" exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0
        sleep 10
    done
    echo "guest agent did not come back" >&2; return 1
}
gpu_state() { # gpu_state NAME: Get-GpuState.ps1 plus the installer's own files
    ps 'C:\win3d\guest\Get-GpuState.ps1 -OutFile C:\win3d\results\gpu.json | Out-Null; Get-Content C:\win3d\results\gpu.json -Raw' >"$EVID/$1-gpu-state.json"
    ps '"## state.json"; if (Test-Path C:\ProgramData\vmlab-win3d\state.json) { Get-Content C:\ProgramData\vmlab-win3d\state.json -Raw } else { "(none)" }
        "## driver store (viogpu)"; Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -match "viogpu" } | ForEach-Object { "{0} {1} {2}" -f $_.Driver, $_.Version, $_.OriginalFileName }
        "## scheduled tasks"; Get-ScheduledTask -TaskName "vmlab-win3d-*" -ErrorAction SilentlyContinue | ForEach-Object { "{0} {1}" -f $_.TaskName, $_.State }
        "## d3d12.dll owner and hash"; foreach ($p in "C:\Windows\System32\d3d12.dll", "C:\Windows\SysWOW64\d3d12.dll") { $a = Get-Acl $p; "{0} {1} {2}" -f $p, $a.Owner, (Get-FileHash $p).Hash; "  " + $a.Sddl }
        "## private dirs"; foreach ($d in "C:\Windows\System32\vmlab-win3d", "C:\Windows\SysWOW64\vmlab-win3d") { if (Test-Path $d) { Get-ChildItem $d | ForEach-Object { "{0} {1}" -f $_.FullName, (Get-FileHash $_.FullName).Hash } } else { "$d absent" } }
        "## services"; Get-Service VioGpu3D, vgpusrv -ErrorAction SilentlyContinue | ForEach-Object { "{0} {1}" -f $_.Name, $_.Status }
        "## certificates"; Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher -ErrorAction SilentlyContinue | Where-Object Subject -match "vmlab-win3d" | ForEach-Object { "{0} {1}" -f $_.PSParentPath.Split(":")[-1], $_.Thumbprint }
        "## powercfg /a"; powercfg.exe /a' >"$EVID/$1-installer-state.txt"
}

say "copy the guest scripts and probes"
wait_agent
ps 'New-Item -ItemType Directory -Force C:\win3d\results | Out-Null' >/dev/null
"$VMLAB" cp "$REPO/tests/guest" "$VM:C:/win3d/guest"
"$VMLAB" cp "$REPO/out/probes" "$VM:C:/win3d/probes"

say "00 test desktop and baseline"
if ! ps '(Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon").AutoAdminLogon' | grep -q 1; then
    "$VMLAB" exec --timeout 300 "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >"$EVID/00-test-desktop.txt" 2>&1 || true
    sleep 60; wait_agent; sleep 60
fi
gpu_state 00-baseline

say "01 install"
"$REPO/scripts/install-into-vm.sh" "${BUNDLE_ARGS[@]}" "$LABDIR" "$VM" 2>&1 | tee "$EVID/01-install.log"
[ "${PIPESTATUS[0]}" -eq 0 ]
gpu_state 02-installed
ps 'Get-Content C:\ProgramData\vmlab-win3d\journal.log' >"$EVID/02-journal.log"

say "03 probes"
"$VMLAB" exec --timeout 1800 "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\win3d\guest\Run-AllProbes.ps1' \
    -ProbeDir 'C:\win3d\probes' -OutDir 'C:\win3d\results\probes' -Seconds 15 >"$EVID/03-probes.txt" 2>&1 || echo "(Run-AllProbes exit $?)" >>"$EVID/03-probes.txt"
ps 'Get-Content C:\win3d\results\probes\summary.json -Raw' >"$EVID/03-probes-summary.json" || true

say "04 install again (idempotence)"
"$REPO/scripts/install-into-vm.sh" "${BUNDLE_ARGS[@]}" "$LABDIR" "$VM" 2>&1 | tee "$EVID/04-reinstall.log"
[ "${PIPESTATUS[0]}" -eq 0 ]
gpu_state 05-reinstalled

say "06 uninstall"
"$REPO/scripts/install-into-vm.sh" --uninstall "$LABDIR" "$VM" 2>&1 | tee "$EVID/06-uninstall.log"
[ "${PIPESTATUS[0]}" -eq 0 ]
gpu_state 07-uninstalled
ps 'Get-Content C:\ProgramData\vmlab-win3d\journal.log' >"$EVID/07-journal.log"
python3 "$REPO/scripts/compare-gpu-state.py" "$EVID/00-baseline-gpu-state.json" "$EVID/07-uninstalled-gpu-state.json" | tee "$EVID/07-compare-baseline.txt"

say "08 install after uninstall"
"$REPO/scripts/install-into-vm.sh" "${BUNDLE_ARGS[@]}" "$LABDIR" "$VM" 2>&1 | tee "$EVID/08-install-again.log"
[ "${PIPESTATUS[0]}" -eq 0 ]
gpu_state 09-installed-again
say "evidence in $EVID"
