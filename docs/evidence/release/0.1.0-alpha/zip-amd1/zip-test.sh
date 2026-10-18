#!/usr/bin/env bash
# Release proof: the driver zip in a fresh AMD1 lab from labs/probe/vmlab.wcl,
# installed the way its README.txt says, then Run-AllProbes.ps1.
#   .scratch/zip-test.sh <zip> <probes-dir> <evidence-dir>
set -uo pipefail
WT=/home/wil/dev/vmlab-win3d-wt/release
ZIP=$1; PROBES=$2; EV=$3
LABDIR=$WT/.scratch/lab-zip
VM=win11
mkdir -p "$LABDIR" "$EV"
exec > >(tee "$EV/run.log") 2>&1
say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
wait_agent() { for _ in $(seq 1 90); do in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0; sleep 10; done; say "agent did not answer"; return 1; }

sed 's/"win3d-host-probe"/"win3d-release-zip"/' "$WT/labs/probe/vmlab.wcl" >"$LABDIR/vmlab.wcl"
cp "$LABDIR/vmlab.wcl" "$EV/lab-vmlab.wcl"
say "lab up"; in_lab vmlab validate && in_lab vmlab up || exit 1
in_lab vmlab status -v >"$EV/status-v.txt" 2>&1
wait_agent || exit 1

say "test desktop (autologon user, for the probes)"
in_lab vmlab cp "$WT/tests/guest" "$VM:C:/win3d/guest"
in_lab vmlab exec --timeout 1200 "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot | tr -d '\r' >"$EV/test-desktop.txt" 2>&1
sleep 60; wait_agent || exit 1

say "copy the zip and extract it (README step 1)"
in_lab vmlab cp "$ZIP" "$VM:C:/Users/Public/Downloads/$(basename "$ZIP")"
gps "Expand-Archive -LiteralPath 'C:\\Users\\Public\\Downloads\\$(basename "$ZIP")' -DestinationPath 'C:\\vmlab-win3d-0.1.0-alpha' -Force; Get-ChildItem -Recurse C:\\vmlab-win3d-0.1.0-alpha -Depth 1 | Select-Object -ExpandProperty FullName" | tee "$EV/extracted.txt"

say "run Install.cmd elevated (README step 2)"
rc=0
in_lab vmlab exec --timeout 1200 "$VM" -- cmd.exe /c 'C:\vmlab-win3d-0.1.0-alpha\vmlab-win3d-0.1.0\Install.cmd' 2>&1 | tr -d '\r' | tee "$EV/install-cmd.txt" || rc=$?
say "Install.cmd exit $rc"

say "wait for Status = Installed (README steps 3 and 4)"
status=
for _ in $(seq 1 160); do
    status=$(gps 'powershell -ExecutionPolicy Bypass -File C:\ProgramData\vmlab-win3d\vmlab-win3d.ps1 -Mode Status' 60 2>/dev/null)
    s1=$(printf '%s' "$status" | tr '\n' ' ' | cut -c1-200)
    say "status: $s1"
    case $status in *Installed*) break ;; *Failed*) break ;; esac
    sleep 15
done
printf '%s\n' "$status" >"$EV/status.txt"
gps 'Get-Content C:\ProgramData\vmlab-win3d\journal.log' >"$EV/journal.log" 2>&1
wait_agent

say "vulkaninfo --summary (README check)"
gps 'vulkaninfo --summary' 120 >"$EV/vulkaninfo-summary.txt" 2>&1
grep -E 'deviceName|driverName' "$EV/vulkaninfo-summary.txt"

say "Run-AllProbes.ps1"
in_lab vmlab cp "$PROBES/x64" "$VM:C:/win3d/probes/x64"
in_lab vmlab cp "$PROBES/x86" "$VM:C:/win3d/probes/x86"
# Wait for the autologon desktop after the install's reboots.
for _ in $(seq 1 40); do
    r=$(gps '[bool](Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0)' 60 2>/dev/null | tail -1)
    [ "$r" = True ] && break; sleep 15
done
sleep 60
in_lab vmlab exec --timeout 1800 "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\win3d\guest\Run-AllProbes.ps1' -ProbeDir 'C:\win3d\probes' -OutDir 'C:\win3d\results' -Seconds 15 2>&1 | tr -d '\r' | tee "$EV/run-allprobes.txt"
echo "exit: ${PIPESTATUS[0]}" >>"$EV/run-allprobes.txt"
gps 'Get-Content C:\win3d\results\summary.json -Raw' >"$EV/summary.json" 2>&1
in_lab vmlab vm screenshot "$VM" "$EV/console-after-probes.png" || true
say "done"
