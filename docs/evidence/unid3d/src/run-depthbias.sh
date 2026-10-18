#!/usr/bin/env bash
# Run d3d9_depthbias.exe (built by build.sh) in a lab with the package
# installed, in the autologon user's session (tests/guest/Run-Probe.ps1),
# on the hardware device (x64 and x86) and on Microsoft's reference
# rasterizer (x64, d3dref9.dll extracted by
# docs/evidence/ffp2/src/run-repro.sh into .scratch/ffp-ref; run that once,
# or copy d3dref9.dll there by hand).
#
#   docs/evidence/unid3d/src/run-depthbias.sh <lab-dir> <vm> <out-dir>
#
# Writes d3d9_depthbias-<run>.txt and the Yttrium log lines of the runs.
# Exit 0 when every run reports failed=0.
set -uo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
lab=$(cd "$1" && pwd) vm=$2
mkdir -p "$3"; out=$(cd "$3" && pwd)
cd "$lab"
gps() { vmlab exec --timeout "${2:-300}" "$vm" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
mark="---- run-depthbias.sh start $(date +%s)"
gps "Remove-Item -Recurse -Force C:\\win3d\\unid3d, C:\\win3d\\results\\unid3d -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\\win3d\\results\\unid3d | Out-Null
\$l = 'C:\\ProgramData\\Yttrium\\yttrium-errors.log'; if (Test-Path \$l) { icacls \$l /grant 'Users:(M)' /Q | Out-Null; Add-Content \$l '$mark' }" >/dev/null
vmlab cp "$repo/tests/guest" "$vm:C:/win3d/guest" >/dev/null
vmlab cp "$repo/.scratch/unid3d-probes" "$vm:C:/win3d/unid3d" >/dev/null
runs="x64-hw x86-hw"
ref=$repo/.scratch/ffp-ref
if [ -f "$ref/x64/d3dref9.dll" ]; then
    vmlab cp "$ref/x64/d3dref9.dll" "$vm:C:/Windows/System32/d3dref9.dll" >/dev/null && runs="$runs x64-ref"
fi
status=0
for run in $runs; do
    arch=${run%%-*} mode=${run#*-} name=d3d9_depthbias-$run
    args="'--log','C:\\win3d\\results\\unid3d\\$name.txt'"
    [ "$mode" = ref ] && args="$args,'--ref'"
    gps "& C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\win3d\\unid3d\\$arch\\d3d9_depthbias.exe -Raw -NoModules -Name $name -OutDir C:\\win3d\\results\\unid3d -TimeoutSec 300 -ArgumentList @($args) | Out-Null; (Get-Content C:\\win3d\\results\\unid3d\\$name.run.json -Raw | ConvertFrom-Json).exit_code" 600 >"$out/$name.exit"
    vmlab cp "$vm:C:/win3d/results/unid3d/$name.txt" "$out/$name.txt" >/dev/null 2>&1 || echo "no log" >"$out/$name.txt"
    tr -d '\r' <"$out/$name.txt" >"$out/$name.txt.tmp" && mv "$out/$name.txt.tmp" "$out/$name.txt"
    printf '%-28s exit %s  %s\n' "$name" "$(tail -1 "$out/$name.exit")" "$(tail -1 "$out/$name.txt")"
    grep -q 'RESULT .* failed=0$' "$out/$name.txt" || status=1
done
gps "\$l = Get-Content C:\\ProgramData\\Yttrium\\yttrium-errors.log -ErrorAction SilentlyContinue; \$i = [array]::LastIndexOf([string[]]\$l, '$mark'); if (\$i -ge 0) { \$l[\$i..(\$l.Count - 1)] | Where-Object { \$_ -match 'd3d9_depthbias' } }" >"$out/yttrium-errors-depthbias.txt" 2>&1
exit $status
