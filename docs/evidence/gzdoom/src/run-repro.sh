#!/usr/bin/env bash
# Run gzgl.exe (built by build.sh) for x64 and x86 in a lab with the package
# installed, in the autologon user's session (tests/guest/Run-Probe.ps1), and
# copy each log to <out-dir>/gzgl-<arch>.txt.
#
#   docs/evidence/gzdoom/src/run-repro.sh <lab-dir> <vm> <out-dir> [gzgl args...]
#
# Exit 0 when both runs pass.
set -uo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
lab=$(cd "$1" && pwd) vm=$2
mkdir -p "$3"; out=$(cd "$3" && pwd)
shift 3
extra=
for a in "$@"; do extra="$extra,'$a'"; done
cd "$lab"
gps() { vmlab exec --timeout "${2:-300}" "$vm" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
gps 'Remove-Item -Recurse -Force C:\win3d\gzgl, C:\win3d\results\gzgl -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\win3d\results\gzgl | Out-Null' >/dev/null
vmlab cp "$repo/tests/guest" "$vm:C:/win3d/guest" >/dev/null
vmlab cp "$repo/.scratch/gzdoom-probes" "$vm:C:/win3d/gzgl" >/dev/null
status=0
for arch in x64 x86; do
    name=gzgl-$arch
    gps "& C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\win3d\\gzgl\\$arch\\gzgl.exe -Raw -NoModules -Name $name -OutDir C:\\win3d\\results\\gzgl -TimeoutSec 120 -ArgumentList @('--log','C:\\win3d\\results\\gzgl\\$name.txt'$extra) | Out-Null; (Get-Content C:\\win3d\\results\\gzgl\\$name.run.json -Raw | ConvertFrom-Json).exit_code" 300 >"$out/$name.exit"
    vmlab cp "$vm:C:/win3d/results/gzgl/$name.txt" "$out/$name.txt" >/dev/null 2>&1 || echo "no log" >"$out/$name.txt"
    tr -d '\r' <"$out/$name.txt" >"$out/$name.txt.tmp" && mv "$out/$name.txt.tmp" "$out/$name.txt"
    rc=$(tail -1 "$out/$name.exit")
    printf '%-10s exit %s  %s\n' "$name" "$rc" "$(tail -1 "$out/$name.txt")"
    [ "$rc" = 0 ] || status=1
done
exit $status
