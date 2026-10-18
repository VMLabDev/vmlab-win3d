#!/usr/bin/env bash
# One first-boot run for the dwm heap-corruption repro: a fresh lab with dwm
# crash capture (lab-prep.sh), the package from out/package/ installed, and
# then, in the first boot after the install's final reboot and with nothing
# else run before it, the D3D12 probe several times (the run that crashed dwm
# in repro-nvidia1/) and, when out/d3d12-samples/ exists, the five samples.
#
#   docs/evidence/dwmheap/src/first-boot-run.sh <lab-dir> <lab-name> <host-gpu> <evidence-dir>
#
# Writes to <evidence-dir>: install.log, probe-<n>.json and .run.json,
# app-errors.txt (Application Error 1000 events since the install), dumps.txt
# (C:\dumps, empty when dwm did not crash), dwm.txt (dwm's start time against
# boot time), the render-server lines of qemu.log, and samples/ from
# tests/apps/d3d12/run.sh. The lab is left running; destroy it afterwards.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
[ $# -eq 4 ] || { sed -n '2,15p' "$0" >&2; exit 2; }
mkdir -p "$1" "$4"
LABDIR=$(cd "$1" && pwd) NAME=$2 GPU=$3 EV=$(cd "$4" && pwd)
VM=win11 RUNS=${PROBE_RUNS:-5}
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-300}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

"$REPO/docs/evidence/dwmheap/src/lab-prep.sh" "$LABDIR" "$NAME" "$GPU" || exit 1
"$REPO/scripts/install-into-vm.sh" "$LABDIR" "$VM" 2>&1 | tee "$EV/install.log" || exit 1
for _ in $(seq 1 60); do gps 'query user' 30 2>/dev/null | grep -q Active && break; sleep 10; done
gps '(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString("s")' >"$EV/boot.txt"
in_lab vmlab cp "$REPO/out/probes/x64" "$VM:C:/win3d/probes/x64" >/dev/null
for n in $(seq 1 "$RUNS"); do
    note "d3d12 probe run $n"
    gps 'Remove-Item C:\win3d\results -Recurse -Force -ErrorAction SilentlyContinue
         & C:\win3d\guest\Run-Probe.ps1 -Path C:\win3d\probes\x64\d3d12_probe.exe -Seconds 20 | Out-Null
         Get-Content C:\win3d\results\x64-d3d12_probe.json -ErrorAction SilentlyContinue' 300 >"$EV/probe-$n.json"
    grep -E '"result"|errors' -A1 "$EV/probe-$n.json" | head -4
done
if [ -d "$REPO/out/d3d12-samples/x64" ]; then
    "$REPO/tests/apps/d3d12/run.sh" "$LABDIR" "$VM" "$EV/samples" || true
fi
gps 'Get-WinEvent -FilterHashtable @{LogName="Application"; Id=1000} -ErrorAction SilentlyContinue | % { "{0} {1}" -f $_.TimeCreated.ToString("s"), (($_.Message -split "`n")[0..4] -join " | ") }' 120 >"$EV/app-errors.txt"
gps 'Get-ChildItem C:\dumps | % { "{0} {1} {2}" -f $_.Name, $_.Length, $_.LastWriteTime.ToString("s") }' 60 >"$EV/dumps.txt"
gps 'Get-Process dwm | % { "dwm pid {0} started {1}" -f $_.Id, $_.StartTime.ToString("s") }; "boot " + (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString("s")' 60 >"$EV/dwm.txt"
grep -v 'vmlab-pci-filter\|^vmlab: ' "${XDG_STATE_HOME:-$HOME/.local/state}/vmlab/labs/$NAME/vms/$VM/qemu.log" >"$EV/qemu-render-server.txt" 2>/dev/null
note "app errors:"; cat "$EV/app-errors.txt"; note "dumps:"; cat "$EV/dumps.txt"; cat "$EV/dwm.txt"
