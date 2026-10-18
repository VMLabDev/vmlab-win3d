#!/usr/bin/env bash
# Run d3d9_ffp.exe (built by build.sh) in a lab with the package installed:
# x64 and x86 on the hardware device with hardware vertex processing, x64
# with software vertex processing, and the same three on Microsoft's
# reference rasterizer, in the autologon user's session
# (tests/guest/Run-Probe.ps1; Set-TestDesktop.ps1 must have run). The
# reference device needs d3dref9.dll from the DirectX SDK (June 2010,
# tests/e2e/PINS); this script extracts it from the cached SDK installer and
# copies it into System32 and SysWOW64. Copies the logs to OUT and compares
# each hardware run with the reference run of the same architecture
# (compare.py): compare-x64.txt and compare-x86.txt against the reference
# with hardware vertex processing, compare-x64-swvp.txt against the
# reference with software vertex processing (the runtime's own CPU vertex
# pipeline feeding the reference rasterizer).
#
#   docs/evidence/ffp/src/run-repro.sh <lab-dir> <vm> <out-dir> [--only SUBSTR]
#
# Exit 0 when every hardware case matches its reference, 1 otherwise.
set -uo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
lab=$(cd "$1" && pwd) vm=$2
mkdir -p "$3"; out=$(cd "$3" && pwd)
shift 3
only=
while [ $# -gt 0 ]; do
    case $1 in
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

# The UMD's warning log is created by dwm.exe; let the user's process append
# to it, and mark where this run starts.
gps 'Remove-Item -Recurse -Force C:\win3d\ffp, C:\win3d\results\ffp -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\win3d\results\ffp, C:\win3d\ffp-ref | Out-Null
$l = "C:\ProgramData\Yttrium\yttrium-errors.log"; if (Test-Path $l) { icacls $l /grant "Users:(M)" /Q | Out-Null; Add-Content $l "---- d3d9_ffp run-repro.sh start" }' >/dev/null
vmlab cp "$repo/tests/guest" "$vm:C:/win3d/guest" >/dev/null
vmlab cp "$repo/.scratch/ffp-probes" "$vm:C:/win3d/ffp" >/dev/null
vmlab cp "$ref" "$vm:C:/win3d/ffp-ref" >/dev/null
gps 'foreach ($p in @(@("x64", "System32"), @("x86", "SysWOW64"))) {
         $src = Get-ChildItem -Recurse -Filter d3dref9.dll C:\win3d\ffp-ref | Where-Object { $_.Directory.Name -eq $p[0] } | Select-Object -First 1
         Copy-Item -LiteralPath $src.FullName -Destination "$env:windir\$($p[1])\d3dref9.dll" -Force }' >/dev/null

runs=(x64-hw x86-hw x64-swvp x64-ref x86-ref x64-refswvp)
for run in "${runs[@]}"; do
    arch=${run%%-*} mode=${run#*-}
    name=d3d9_ffp-$run
    args="'--log','C:\\win3d\\results\\ffp\\$name.txt'"
    case $mode in
        ref) args="$args,'--ref'" ;;
        swvp) args="$args,'--swvp'" ;;
        refswvp) args="$args,'--ref','--swvp'" ;;
    esac
    [ -n "$only" ] && args="$args,'--only','$only'"
    gps "& C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\win3d\\ffp\\$arch\\d3d9_ffp.exe -Raw -NoModules -Name $name -OutDir C:\\win3d\\results\\ffp -TimeoutSec 900 -ArgumentList @($args) | Out-Null; (Get-Content C:\\win3d\\results\\ffp\\$name.run.json -Raw | ConvertFrom-Json).exit_code" 1200 >"$out/$name.exit"
    vmlab cp "$vm:C:/win3d/results/ffp/$name.txt" "$out/$name.txt" >/dev/null 2>&1 || echo "no log" >"$out/$name.txt"
    tr -d '\r' <"$out/$name.txt" >"$out/$name.txt.tmp" && mv "$out/$name.txt.tmp" "$out/$name.txt"
    printf '%-22s exit %s  %s\n' "$name" "$(tail -1 "$out/$name.exit")" "$(tail -1 "$out/$name.txt")"
done
status=0
cmp() { # cmp NAME REF HW-RUN...
    local name=$1 ref=$2; shift 2
    local hw=()
    for r in "$@"; do hw+=("$r=$out/d3d9_ffp-$r.txt"); done
    python3 "$repo/docs/evidence/ffp/src/compare.py" "$out/d3d9_ffp-$ref.txt" "${hw[@]}" >"$out/compare-$name.txt" || status=1
    tail -n "$#" "$out/compare-$name.txt" | sed "s/^/vs $ref: /"
}
cmp x64 x64-ref x64-hw
cmp x86 x86-ref x86-hw
cmp x64-swvp x64-refswvp x64-swvp
cmp x64-hw-vs-refswvp x64-refswvp x64-hw
# The UMD warnings written during the runs, without dwm's.
gps '$l = Get-Content C:\ProgramData\Yttrium\yttrium-errors.log -ErrorAction SilentlyContinue; $i = [array]::LastIndexOf([string[]]$l, "---- d3d9_ffp run-repro.sh start"); if ($i -ge 0) { $l[$i..($l.Count - 1)] | Where-Object { $_ -notmatch "process=dwm.exe" } }' >"$out/yttrium-errors.txt" 2>&1
exit $status
