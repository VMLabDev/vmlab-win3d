#!/usr/bin/env bash
# Run the ExecuteIndirect repro (out/ei/x64/ei_repro.exe, built by build.sh)
# in a lab with the package installed and the autologon desktop up.
#
#   docs/evidence/ei/src/run-repro.sh <lab-dir> <vm> <evidence-dir>
#
# Runs "ei_repro gpu" with the system d3d12.dll (the package's bridge), then
# "ei_repro warp" from C:\ei\warp, which holds Microsoft's d3d12.dll (the
# package's preserved System32\vmlab-win3d\d3d12-native.dll, plus a copy of
# System32\D3D12Core.dll: outside System32 the stock d3d12.dll looks for its
# core next to itself and fails with 0x887E0003 otherwise), then
# "ei_repro compare". All three run in the user's session through
# tests/guest/Run-Probe.ps1. Writes repro.txt (the three outputs), the vkd3d
# log, and ei_gpu.png / ei_warp.png to <evidence-dir>. Exit 0 when the
# comparison passes.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
[ $# -eq 3 ] || { sed -n '2,14p' "$0" >&2; exit 2; }
LABDIR=$(cd "$1" && pwd)
VM=$2
mkdir -p "$3"
EV=$(cd "$3" && pwd)
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-300}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

gps 'if (Test-Path C:\ei) { Remove-Item C:\ei -Recurse -Force }
     New-Item -ItemType Directory C:\ei\gpu, C:\ei\warp, C:\ei\res -Force | Out-Null
     Copy-Item "$env:windir\System32\vmlab-win3d\d3d12-native.dll" C:\ei\warp\d3d12.dll
     Copy-Item "$env:windir\System32\D3D12Core.dll" C:\ei\warp\' >/dev/null
in_lab vmlab cp "$REPO/out/ei/x64/ei_repro.exe" "$VM:C:/ei/gpu/ei_repro.exe" >/dev/null
in_lab vmlab cp "$REPO/out/ei/x64/ei_repro.exe" "$VM:C:/ei/warp/ei_repro.exe" >/dev/null
in_lab vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest" >/dev/null
gps 'Set-Content -Encoding ascii C:\ei\run.cmd -Value @(
       "@echo off",
       "set VKD3D_DEBUG=info",
       "C:\ei\gpu\ei_repro.exe gpu C:\ei > C:\ei\gpu.txt 2> C:\ei\vkd3d-gpu.txt",
       "set VKD3D_DEBUG=",
       "C:\ei\warp\ei_repro.exe warp C:\ei > C:\ei\warp.txt 2>&1",
       "C:\ei\gpu\ei_repro.exe compare C:\ei > C:\ei\compare.txt 2>&1",
       "echo %errorlevel% > C:\ei\rc.txt")' >/dev/null
in_lab vmlab exec --timeout 400 "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "& C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\Windows\\System32\\cmd.exe -ArgumentList '/c','C:\\ei\\run.cmd' -Raw -Name ei_repro -OutDir C:\\ei\\res -Seconds 240 -TimeoutSec 300 -NoModules; exit \$LASTEXITCODE" >/dev/null 2>&1
gps 'foreach ($f in "gpu", "warp", "compare") { "== ei_repro $f"; Get-Content "C:\ei\$f.txt" }
     "== exit " + (Get-Content C:\ei\rc.txt)' | tee "$EV/repro.txt"
gps 'Get-Content C:\ei\vkd3d-gpu.txt' >"$EV/vkd3d-gpu.txt"
for f in gpu warp; do
    in_lab vmlab cp "$VM:C:/ei/ei_$f.ppm" "$EV/ei_$f.ppm" >/dev/null 2>&1 &&
        magick "$EV/ei_$f.ppm" "$EV/ei_$f.png" && rm -f "$EV/ei_$f.ppm"
done
grep -q '^RESULT PASS' "$EV/repro.txt"
