#!/usr/bin/env bash
# Run one DirectDraw repro in the autologon desktop of a lab kept by
# `tests/e2e/run.sh --keep`, with console captures during the run.
#
#   run-guest.sh <lab-dir> <out-dir> <arch> <exe> <seconds> [args...]
#
# <exe> is ddfull, ddblit or ddcaps from out/ddraw/<arch>/
# (docs/evidence/ddraw/src/build.sh); the directory is copied to
# C:\win3d\ddraw in the guest when its hashes differ. Run-Probe.ps1 runs it
# with --seconds <seconds> --out ...; [args...] follow. WIN3D_TRACE=1 turns
# on the D3D9 UMD trace for the run. WIN3D_UMD=<dir> first copies
# <dir>/{x64,x86}/viogpu_d3d10.dll over the installed UMD (development only).
# Writes <out-dir>/<exe>-<arch>.json, .run.json, console-*.png every 2 s,
# yttrium-user.log (with WIN3D_TRACE) and the host's NVRM kernel lines.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
LABDIR=$(cd "$1" && pwd); mkdir -p "$2"; OUT=$(cd "$2" && pwd); ARCH=$3; EXE=$4; SECS=$5; shift 5
VM=win
NAME=$EXE-$ARCH
mkdir -p "$OUT"
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

want=$(cd "$REPO/out/ddraw" && sha256sum x64/*.exe x86/*.exe | sha256sum | cut -c1-16)
have=$(gps 'Get-Content C:\win3d\ddraw\stamp -ErrorAction SilentlyContinue' 60 | tail -1)
if [ "$want" != "$have" ]; then
    note "copying out/ddraw to C:\\win3d\\ddraw"
    gps 'if (Test-Path C:\win3d\ddraw) { Remove-Item -Recurse -Force C:\win3d\ddraw }' 60 >/dev/null
    in_lab vmlab cp "$REPO/out/ddraw" "$VM:C:/win3d/ddraw" || exit 1
    gps "Set-Content C:\\win3d\\ddraw\\stamp '$want'" 60 >/dev/null
fi
if [ -n "${WIN3D_UMD:-}" ]; then
    WIN3D_UMD=$(cd "$WIN3D_UMD" && pwd)
    note "replacing the installed UMD with $WIN3D_UMD"
    in_lab vmlab cp "$WIN3D_UMD/x64/viogpu_d3d10.dll" "$VM:C:/win3d/umd-x64.dll" || exit 1
    in_lab vmlab cp "$WIN3D_UMD/x86/viogpu_d3d10.dll" "$VM:C:/win3d/umd-x86.dll" || exit 1
    # In-use DLLs (dwm has the UMD loaded) can be renamed but not overwritten.
    gps '$ErrorActionPreference = "Stop"
         foreach ($p in @(@("C:\Windows\System32\viogpu_d3d10.dll", "C:\win3d\umd-x64.dll"),
                          @("C:\Windows\SysWOW64\viogpu_d3d10.dll", "C:\win3d\umd-x86.dll"))) {
             takeown /f $p[0] | Out-Null; icacls $p[0] /grant "Administrators:F" | Out-Null
             $old = $p[0] + "." + [DateTime]::Now.Ticks + ".old"
             Rename-Item $p[0] (Split-Path -Leaf $old); Copy-Item $p[1] $p[0]
             "{0} {1}" -f (Get-FileHash $p[0]).Hash, $p[0] }' 120
fi
if [ -n "${WIN3D_TRACE:-}" ]; then
    gps 'New-Item -ItemType Directory -Force C:\ProgramData\Yttrium | Out-Null
         Set-Content C:\ProgramData\Yttrium\yttrium.ini "D3D10UMD_D3D9_TRACE=1" -Encoding ASCII
         icacls C:\ProgramData\Yttrium /grant "Users:(OI)(CI)M" | Out-Null
         Remove-Item -Force -ErrorAction SilentlyContinue C:\ProgramData\Yttrium\yttrium-user.log' 60 >/dev/null
fi

arglist=
[ $# -gt 0 ] && arglist="-ArgumentList $(printf "'%s'," "$@" | sed 's/,$//')"
since=$(date '+%Y-%m-%d %H:%M:%S')
note "run $NAME ($SECS s) $*"
in_lab vmlab exec --timeout $((SECS + 400)) "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "& C:\\win3d\\guest\\Run-Probe.ps1 -Path 'C:\\win3d\\ddraw\\$ARCH\\$EXE.exe' $arglist -Name $NAME -OutDir C:\\win3d\\results\\ddraw -Seconds $SECS -TimeoutSec $((SECS + 300)) -NoModules; exit \$LASTEXITCODE" \
    >"$OUT/$NAME.run-probe.out" 2>&1 &
pid=$!; n=0
while kill -0 "$pid" 2>/dev/null; do
    sleep 2; n=$((n + 1))
    timeout 20 bash -c "cd '$LABDIR' && vmlab vm screenshot $VM '$OUT/console-$NAME-$(printf %03d $n).png'" >/dev/null 2>&1
done
wait "$pid"; rc=$?
note "Run-Probe exit $rc"
journalctl -k --since "$since" --no-pager 2>/dev/null | grep -a NVRM >"$OUT/kernel-nvrm-$NAME.txt"
[ -s "$OUT/kernel-nvrm-$NAME.txt" ] && { note "host kernel NVRM lines:"; cat "$OUT/kernel-nvrm-$NAME.txt"; }
in_lab vmlab cp "$VM:C:/win3d/results/ddraw/$NAME.json" "$OUT/$NAME.json" >/dev/null 2>&1
in_lab vmlab cp "$VM:C:/win3d/results/ddraw/$NAME.run.json" "$OUT/$NAME.run.json" >/dev/null 2>&1
if [ -n "${WIN3D_TRACE:-}" ]; then
    in_lab vmlab cp "$VM:C:/ProgramData/Yttrium/yttrium-user.log" "$OUT/yttrium-user-$NAME.log" >/dev/null 2>&1
    gps 'Remove-Item -Force -ErrorAction SilentlyContinue C:\ProgramData\Yttrium\yttrium.ini' 60 >/dev/null
fi
[ -f "$OUT/$NAME.json" ] && python3 -c 'import json,sys; j=json.load(open(sys.argv[1])); print("pass:", j.get("pass"))' "$OUT/$NAME.json"
exit "$rc"
