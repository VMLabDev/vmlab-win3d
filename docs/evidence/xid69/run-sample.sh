#!/usr/bin/env bash
# Run one program in the autologon desktop of a lab kept by
# `tests/e2e/run.sh --keep`, with console captures, for the Xid 69 study.
#
#   run-sample.sh <lab-dir> <name> <seconds> <guest exe> [args...]
#
# Stages the DirectX SDK (June 2010) samples and their runtimes the way
# tests/e2e/apps.sh does, once per guest (C:\apps\DXSDK present = staged).
# Writes .scratch/xid69/<name>/: console captures every 3 s, the run
# summary, and the host kernel's NVRM lines logged during the run.
# WIN3D_GUEST_ENV="NAME=VALUE ..." sets machine environment variables in
# the guest first (seen by the newly started program, not by dwm).
# WIN3D_NO_STAGE=1 skips the SDK staging (for the bisect probe).
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
LABDIR=$(cd "$1" && pwd); NAME=$2; SECS=$3; EXE=$4; shift 4
VM=win
OUT=$REPO/.scratch/xid69/$NAME
CACHE=$REPO/.scratch/e2e/cache
STAGE=$REPO/.scratch/e2e/apps
mkdir -p "$OUT"; find "$OUT" -mindepth 1 -delete
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
fetch_pin() {
    local name=$1 sha url f
    read -r _ sha url < <(grep "^$name " "$REPO/tests/e2e/PINS") || return 1
    f=$CACHE/$(basename "$url"); mkdir -p "$CACHE"
    [ -f "$f" ] || { curl -sSfLo "$f.part" "$url" && mv "$f.part" "$f"; } || return 1
    echo "$sha  $f" | sha256sum -c --quiet - >&2 || return 1
    echo "$f"
}

if [ -z "${WIN3D_NO_STAGE:-}" ] && ! gps 'Test-Path C:\apps\DXSDK\Samples' 60 | grep -q True; then
    note "staging the DirectX SDK samples and runtimes"
    sdk=$(fetch_pin dxsdk-jun2010) && vc64=$(fetch_pin vcredist2010-x64) && vc86=$(fetch_pin vcredist2010-x86) || exit 1
    mkdir -p "$STAGE"
    if [ ! -f "$STAGE/dxsdk-2.zip" ]; then
        rm -rf "$STAGE/dxsdk"
        7z x -y -o"$STAGE/dxsdk" "$sdk" "DXSDK/Redist/*" "DXSDK/Samples/Media/*" \
            "DXSDK/Samples/C++/Direct3D/*" "DXSDK/Samples/C++/Direct3D11/*" \
            "DXSDK/Developer Runtime/*" >/dev/null || exit 1
        (cd "$STAGE/dxsdk" && bsdtar -a -cf "$STAGE/dxsdk-2.zip" --options zip:compression=store DXSDK) || exit 1
        rm -rf "$STAGE/dxsdk"
    fi
    in_lab vmlab cp "$STAGE/dxsdk-2.zip" "$VM:C:/apps/dxsdk.zip" || exit 1
    in_lab vmlab cp "$vc64" "$VM:C:/apps/vcredist_x64.exe" || exit 1
    in_lab vmlab cp "$vc86" "$VM:C:/apps/vcredist_x86.exe" || exit 1
    gps 'Set-Location C:\apps; tar.exe -xf dxsdk.zip; Remove-Item dxsdk.zip
         $p = Start-Process C:\apps\DXSDK\Redist\DXSETUP.exe -ArgumentList "/silent" -Wait -PassThru; "DXSETUP exit {0}" -f $p.ExitCode
         foreach ($a in "x64", "x86") { $p = Start-Process "C:\apps\vcredist_$a.exe" -ArgumentList "/q", "/norestart" -Wait -PassThru; "vcredist_$a exit {0}" -f $p.ExitCode }' 1200
fi

if [ -n "${WIN3D_GUEST_ENV:-}" ]; then
    for kv in $WIN3D_GUEST_ENV; do
        gps "[Environment]::SetEnvironmentVariable('${kv%%=*}', '${kv#*=}', 'Machine')" 60
    done
fi
gps 'Remove-Item -Force -ErrorAction SilentlyContinue C:\ProgramData\Yttrium\yttrium-user.log; icacls C:\ProgramData\Yttrium /grant "Users:(OI)(CI)M" | Out-Null' 60

arglist=
[ $# -gt 0 ] && arglist="-ArgumentList $(printf "'%s'," "$@" | sed 's/,$//')"
since=$(date '+%Y-%m-%d %H:%M:%S')
note "run $NAME: $EXE $* ($SECS s)"
in_lab vmlab exec --timeout $((SECS + 200)) "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "& C:\\win3d\\guest\\Run-Probe.ps1 -Path '$EXE' $arglist -Raw -Name $NAME -OutDir C:\\win3d\\results\\xid69 -Seconds $SECS -TimeoutSec $SECS; exit \$LASTEXITCODE" \
    >"$OUT/run-probe.out" 2>&1 &
pid=$!; n=0
while kill -0 "$pid" 2>/dev/null; do
    sleep 3; n=$((n + 1))
    timeout 20 bash -c "cd '$LABDIR' && vmlab vm screenshot $VM '$OUT/shot-$(printf %03d $n).png'" >/dev/null 2>&1
done
wait "$pid"; rc=$?
note "Run-Probe exit $rc (124 = still running at $SECS s, then closed)"
echo "$rc" >"$OUT/rc"
journalctl -k --since "$since" --no-pager 2>/dev/null | grep -a NVRM >"$OUT/kernel-nvrm.txt"
[ -s "$OUT/kernel-nvrm.txt" ] && { note "host kernel NVRM lines:"; cat "$OUT/kernel-nvrm.txt"; }
in_lab vmlab cp "$VM:C:/win3d/results/xid69/$NAME.run.json" "$OUT/run.json" >/dev/null 2>&1
in_lab vmlab cp "$VM:C:/ProgramData/Yttrium/yttrium-user.log" "$OUT/yttrium-user.log" >/dev/null 2>&1
if [ -n "${WIN3D_GUEST_ENV:-}" ]; then
    for kv in $WIN3D_GUEST_ENV; do
        gps "[Environment]::SetEnvironmentVariable('${kv%%=*}', \$null, 'Machine')" 60 >/dev/null
    done
fi
exit 0
