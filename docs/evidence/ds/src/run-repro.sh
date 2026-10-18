#!/usr/bin/env bash
# Run d3d11_index.exe (built by build.sh) in a lab with the package
# installed: x64 and x86, on the hardware device and on WARP, in the
# autologon user's session (tests/guest/Run-Probe.ps1; Set-TestDesktop.ps1
# must have run). Copies the logs to OUT and compares every read-back value
# of the hardware run with the WARP run.
#
#   docs/evidence/ds/src/run-repro.sh <lab-dir> <vm> <out-dir>
#
# Exit 0 when all four runs pass and hardware matches WARP, 1 otherwise.
set -uo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
lab=$(cd "$1" && pwd) vm=$2
mkdir -p "$3"; out=$(cd "$3" && pwd)
cd "$lab"
gps() { vmlab exec --timeout "${2:-300}" "$vm" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

# The UMD's warning log is created by dwm.exe; let the user's process append
# to it, and mark where this run starts.
gps 'Remove-Item -Recurse -Force C:\win3d\ds, C:\win3d\results\ds -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\win3d\results\ds\asm | Out-Null
$l = "C:\ProgramData\Yttrium\yttrium-errors.log"; if (Test-Path $l) { icacls $l /grant "Users:(M)" /Q | Out-Null; Add-Content $l "---- d3d11_index run-repro.sh start" }' >/dev/null
vmlab cp "$repo/tests/guest" "$vm:C:/win3d/guest" >/dev/null
vmlab cp "$repo/.scratch/ds-probes" "$vm:C:/win3d/ds" >/dev/null
status=0
for arch in x64 x86; do
    for mode in hw warp; do
        name=d3d11_index-$arch-$mode
        args="'--log','C:\\win3d\\results\\ds\\$name.txt'"
        [ "$mode" = warp ] && args="'--warp',$args"
        [ "$arch-$mode" = x64-hw ] && args="$args,'--asm','C:\\win3d\\results\\ds\\asm'"
        gps "& C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\win3d\\ds\\$arch\\d3d11_index.exe -Raw -NoModules -Name $name -OutDir C:\\win3d\\results\\ds -TimeoutSec 240 -ArgumentList @($args) | Out-Null; (Get-Content C:\\win3d\\results\\ds\\$name.run.json -Raw | ConvertFrom-Json).exit_code" 600 >"$out/$name.exit"
        vmlab cp "$vm:C:/win3d/results/ds/$name.txt" "$out/$name.txt" >/dev/null 2>&1 || echo "no log" >"$out/$name.txt"
        tr -d '\r' <"$out/$name.txt" >"$out/$name.txt.tmp" && mv "$out/$name.txt.tmp" "$out/$name.txt"
        printf '%-26s exit %s  %s\n' "$name" "$(tail -1 "$out/$name.exit")" "$(tail -1 "$out/$name.txt")"
        grep -q '^RESULT' "$out/$name.txt" && ! grep -q '^RESULT .* FAIL' "$out/$name.txt" || status=1
    done
    # Hardware against WARP: every read-back line must be the same.
    if diff <(grep -E '^  sel|^RESULT' "$out/d3d11_index-$arch-hw.txt") \
            <(grep -E '^  sel|^RESULT' "$out/d3d11_index-$arch-warp.txt") >"$out/d3d11_index-$arch-hw-vs-warp.diff"; then
        echo "$arch: hardware matches WARP"
    else
        echo "$arch: hardware differs from WARP (d3d11_index-$arch-hw-vs-warp.diff)"; status=1
    fi
done
for f in ds-vpc-ds_5_0 ds-vcp-ds_5_0 ds-vpc-loop-ds_5_0 gs-in-gs_5_0 ps-in-ps_5_0; do
    mkdir -p "$out/asm"
    vmlab cp "$vm:C:/win3d/results/ds/asm/$f.asm.txt" "$out/asm/$f.asm.txt" >/dev/null 2>&1 &&
        tr -d '\r' <"$out/asm/$f.asm.txt" >"$out/asm/$f.tmp" && mv "$out/asm/$f.tmp" "$out/asm/$f.asm.txt"
done
# The UMD warnings written during the runs, without dwm's.
gps '$l = Get-Content C:\ProgramData\Yttrium\yttrium-errors.log -ErrorAction SilentlyContinue; $i = [array]::LastIndexOf([string[]]$l, "---- d3d11_index run-repro.sh start"); if ($i -ge 0) { $l[$i..($l.Count - 1)] | Where-Object { $_ -notmatch "process=dwm.exe" } }' >"$out/yttrium-errors.txt" 2>&1
exit $status
