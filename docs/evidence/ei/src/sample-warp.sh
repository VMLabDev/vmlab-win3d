#!/usr/bin/env bash
# Run the unmodified D3D12ExecuteIndirect sample on WARP in a lab where
# tests/apps/d3d12/run.sh has already staged the samples, and compare its
# console capture with the GPU run's (compare-sample.py).
#
#   docs/evidence/ei/src/sample-warp.sh <lab-dir> <vm> <evidence-dir>
#
# <evidence-dir> must hold console-D3D12ExecuteIndirect.png from run.sh.
# The WARP copy runs from C:\apps\ei-warp with Microsoft's d3d12.dll (the
# package's preserved System32\vmlab-win3d\d3d12-native.dll) and with the
# sample's own -warp switch. The samples export D3D12SDKVersion = 618 and
# D3D12SDKPath = .\D3D12\, so outside System32 the stock d3d12.dll needs
# the Agility SDK core there: D3D12Core.dll from the pinned
# Microsoft.Direct3D.D3D12 1.618.3 package that build/d3d12-samples
# downloads (without it every sample fails with 0xC0000409).
# Writes console-D3D12ExecuteIndirect-warp.png and sample-compare.txt.
# Exit status: compare-sample.py's.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
[ $# -eq 3 ] || { sed -n '2,13p' "$0" >&2; exit 2; }
LABDIR=$(cd "$1" && pwd)
VM=$2
EV=$(cd "$3" && pwd)
SECS=30
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-300}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

NUPKG=$(ls "$REPO"/out/d3d12-samples/.work/downloads/nuget-d3d12-*.nupkg)
grep -q "^nuget-d3d12 .* $(sha256sum "$NUPKG" | cut -d' ' -f1)$" "$REPO/build/d3d12-samples/PINS" \
    || { echo "unpinned $NUPKG" >&2; exit 2; }
TMP=$(mktemp -d)
unzip -o -j -q "$NUPKG" build/native/bin/x64/D3D12Core.dll -d "$TMP"
gps 'if (Test-Path C:\apps\ei-warp) { Remove-Item C:\apps\ei-warp -Recurse -Force }
     Copy-Item C:\apps\d3d12-samples\D3D12ExecuteIndirect C:\apps\ei-warp -Recurse
     New-Item -ItemType Directory C:\apps\ei-warp\D3D12 | Out-Null
     Copy-Item "$env:windir\System32\vmlab-win3d\d3d12-native.dll" C:\apps\ei-warp\d3d12.dll' >/dev/null
in_lab vmlab cp "$TMP/D3D12Core.dll" "$VM:C:/apps/ei-warp/D3D12/D3D12Core.dll" >/dev/null
rm -rf "$TMP"
in_lab vmlab exec --timeout $((SECS + 300)) "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "& C:\\win3d\\guest\\Run-Probe.ps1 -Path 'C:\\apps\\ei-warp\\D3D12ExecuteIndirect.exe' -ArgumentList '-warp' -Raw -Name ei-warp -OutDir C:\\win3d\\results\\ei-warp -Seconds $SECS -TimeoutSec $((SECS + 60))" \
    >"$EV/warp-run.txt" 2>&1 &
pid=$!
shots=()
for i in $(seq 1 6); do
    sleep 4
    f=$EV/.warp-$i.png
    in_lab vmlab vm screenshot "$VM" "$f" >/dev/null 2>&1 && shots+=("$f")
done
# A frame two thirds into the captures, as run.sh picks for the GPU run.
[ ${#shots[@]} -gt 0 ] && cp "${shots[$(( ${#shots[@]} * 2 / 3 ))]}" "$EV/console-D3D12ExecuteIndirect-warp.png"
rm -f "$EV"/.warp-*.png
wait "$pid"
in_lab vmlab cp "$VM:C:/win3d/results/ei-warp/ei-warp.run.json" "$EV/ei-warp.run.json" >/dev/null 2>&1
python3 "$HERE/compare-sample.py" "$EV/console-D3D12ExecuteIndirect.png" "$EV/console-D3D12ExecuteIndirect-warp.png" \
    | tee "$EV/sample-compare.txt"
exit "${PIPESTATUS[0]}"
