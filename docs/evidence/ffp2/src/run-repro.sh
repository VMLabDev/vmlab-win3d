#!/usr/bin/env bash
# Run the fixed-function probes (built by build.sh) in a lab with the
# package installed, in the autologon user's session
# (tests/guest/Run-Probe.ps1; Set-TestDesktop.ps1 must have run), on the
# hardware device and on Microsoft's reference rasterizer, then compare each
# hardware run with the reference run that matches it
# (docs/evidence/ffp/src/compare.py). The reference device needs
# d3dref9.dll from the DirectX SDK (June 2010, tests/e2e/PINS); this script
# extracts it from the cached SDK installer and copies it into System32 and
# SysWOW64.
#
#   docs/evidence/ffp2/src/run-repro.sh <lab-dir> <vm> <out-dir>
#       [--prog d3d9_ffp2|d3d9_ffp] [--runs "RUN..."] [--only SUBSTR]
#
# A run is <arch>-<mode>: arch x64 or x86, mode hw (hardware vertex
# processing), swvp (software), mixhw and mixsw (a mixed device switched to
# hardware or software), and ref, refswvp, refmixhw, refmixsw for the
# same on the reference device. The default runs are
#   x64-hw x86-hw x64-swvp x64-mixhw x64-mixsw x64-ref x86-ref x64-refswvp
# and the comparisons (each made when both logs exist):
#   compare-x64.txt        x64-hw    against x64-ref
#   compare-x86.txt        x86-hw    against x86-ref
#   compare-x64-swvp.txt   x64-swvp  against x64-refswvp
#   compare-x64-mixhw.txt  x64-mixhw against x64-ref
#   compare-x64-mixsw.txt  x64-mixsw against x64-refswvp
#   compare-x64-hw-vs-refswvp.txt  x64-hw against x64-refswvp (the
#                          runtime's own vertex pipeline feeding the
#                          reference rasterizer)
# Logs are <prog>-<run>.txt. Exit 0 when every comparison passes, 1 otherwise.
set -uo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
lab=$(cd "$1" && pwd) vm=$2
mkdir -p "$3"; out=$(cd "$3" && pwd)
shift 3
prog=d3d9_ffp2 only=
runs="x64-hw x86-hw x64-swvp x64-mixhw x64-mixsw x64-ref x86-ref x64-refswvp"
while [ $# -gt 0 ]; do
    case $1 in
        --prog) prog=$2; shift 2 ;;
        --runs) runs=$2; shift 2 ;;
        --only) only=$2; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
cd "$lab"
gps() { vmlab exec --timeout "${2:-300}" "$vm" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

# d3dref9.dll from the pinned SDK installer.
ref=$repo/.scratch/ffp-ref
if [ ! -f "$ref/x64/d3dref9.dll" ] || [ ! -f "$ref/x86/d3dref9.dll" ]; then
    read -r _ sha url < <(grep '^dxsdk-jun2010 ' "$repo/tests/e2e/PINS")
    sdk=$repo/.scratch/e2e/cache/$(basename "$url")
    mkdir -p "$(dirname "$sdk")"
    [ -f "$sdk" ] || { curl -sSfLo "$sdk.part" "$url" && mv "$sdk.part" "$sdk"; } || exit 1
    echo "$sha  $sdk" | sha256sum -c --quiet - || { echo "sha256 mismatch for $sdk" >&2; exit 1; }
    rm -rf "$ref"; mkdir -p "$ref/x64" "$ref/x86"
    7z x -y -o"$ref/sdk" "$sdk" "DXSDK/Developer Runtime/x64/d3dref9.dll" "DXSDK/Developer Runtime/x86/d3dref9.dll" >/dev/null || exit 1
    cp "$ref/sdk/DXSDK/Developer Runtime/x64/d3dref9.dll" "$ref/x64/" &&
        cp "$ref/sdk/DXSDK/Developer Runtime/x86/d3dref9.dll" "$ref/x86/" || exit 1
    rm -rf "$ref/sdk"
fi
sha256sum "$ref"/x64/d3dref9.dll "$ref"/x86/d3dref9.dll | sed "s|$repo/||" >"$out/d3dref9-sha256.txt"

mark="---- $prog run-repro.sh start $(date +%s)"
gps "Remove-Item -Recurse -Force C:\\win3d\\ffp2, C:\\win3d\\results\\ffp2 -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\\win3d\\results\\ffp2, C:\\win3d\\ffp-ref | Out-Null
\$l = 'C:\\ProgramData\\Yttrium\\yttrium-errors.log'; if (Test-Path \$l) { icacls \$l /grant 'Users:(M)' /Q | Out-Null; Add-Content \$l '$mark' }" >/dev/null
vmlab cp "$repo/tests/guest" "$vm:C:/win3d/guest" >/dev/null
vmlab cp "$repo/.scratch/ffp2-probes" "$vm:C:/win3d/ffp2" >/dev/null
vmlab cp "$ref" "$vm:C:/win3d/ffp-ref" >/dev/null
gps 'foreach ($p in @(@("x64", "System32"), @("x86", "SysWOW64"))) {
         $src = Get-ChildItem -Recurse -Filter d3dref9.dll C:\win3d\ffp-ref | Where-Object { $_.Directory.Name -eq $p[0] } | Select-Object -First 1
         Copy-Item -LiteralPath $src.FullName -Destination "$env:windir\$($p[1])\d3dref9.dll" -Force }' >/dev/null

for run in $runs; do
    arch=${run%%-*} mode=${run#*-}
    name=$prog-$run
    args="'--log','C:\\win3d\\results\\ffp2\\$name.txt'"
    case $mode in ref*) args="$args,'--ref'"; mode=${mode#ref} ;; esac
    case $mode in
        hw|'') ;;
        swvp) args="$args,'--swvp'" ;;
        mixhw) args="$args,'--mixed'" ;;
        mixsw) args="$args,'--mixed','--swvp'" ;;
        *) echo "unknown run $run" >&2; exit 2 ;;
    esac
    [ -n "$only" ] && args="$args,'--only','$only'"
    gps "& C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\win3d\\ffp2\\$arch\\$prog.exe -Raw -NoModules -Name $name -OutDir C:\\win3d\\results\\ffp2 -TimeoutSec 900 -ArgumentList @($args) | Out-Null; (Get-Content C:\\win3d\\results\\ffp2\\$name.run.json -Raw | ConvertFrom-Json).exit_code" 1200 >"$out/$name.exit"
    vmlab cp "$vm:C:/win3d/results/ffp2/$name.txt" "$out/$name.txt" >/dev/null 2>&1 || echo "no log" >"$out/$name.txt"
    tr -d '\r' <"$out/$name.txt" >"$out/$name.txt.tmp" && mv "$out/$name.txt.tmp" "$out/$name.txt"
    printf '%-28s exit %s  %s\n' "$name" "$(tail -1 "$out/$name.exit")" "$(tail -1 "$out/$name.txt")"
done
status=0
cmp() { # cmp NAME REF HW
    local name=$1 r=$out/$prog-$2.txt h=$out/$prog-$3.txt
    [ -f "$r" ] && [ -f "$h" ] || return 0
    python3 "$repo/docs/evidence/ffp/src/compare.py" "$r" "$3=$h" >"$out/compare-$prog-$name.txt" || status=1
    tail -n 1 "$out/compare-$prog-$name.txt" | sed "s/^/vs $2: /"
}
cmp x64 x64-ref x64-hw
cmp x86 x86-ref x86-hw
cmp x64-swvp x64-refswvp x64-swvp
cmp x64-mixhw x64-ref x64-mixhw
cmp x64-mixsw x64-refswvp x64-mixsw
cmp x64-hw-vs-refswvp x64-refswvp x64-hw
gps "\$l = Get-Content C:\\ProgramData\\Yttrium\\yttrium-errors.log -ErrorAction SilentlyContinue; \$i = [array]::LastIndexOf([string[]]\$l, '$mark'); if (\$i -ge 0) { \$l[\$i..(\$l.Count - 1)] | Where-Object { \$_ -notmatch 'process=dwm.exe' } }" >"$out/yttrium-errors-$prog.txt" 2>&1
exit $status
