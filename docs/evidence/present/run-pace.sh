#!/usr/bin/env bash
# Run the present repro in the autologon desktop of a lab kept by
# `tests/e2e/run.sh --keep` (which staged C:\win3d\guest\Run-Probe.ps1).
#
#   run-pace.sh <lab-dir> <out-dir> <arch> <case> [seconds [width height]]
#
# <case> is a pace.exe mode (d3d9, d3d9-imm, d3d9ex, ddraw, vblank) or
# gl_alpha. Copies .scratch/present-probes/<arch>/ (docs/evidence/present/
# src/build.sh) to C:\win3d\present\<arch>, runs the case, and writes
# <out-dir>/<case>-<arch>.txt (the probe's report) and a console capture
# <out-dir>/console-<case>-<arch>.png taken halfway through.
# WIN3D_GUEST_ENV="NAME=VALUE ..." sets user environment variables for the
# autologon user first and clears them after.
# Exit status: the probe's (0 PASS, 1 FAIL), or 125 when it did not run.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
LABDIR=$(cd "$1" && pwd); OUT=$2; ARCH=$3; CASE=$4; SECS=${5:-15}; SIZE=${6:+-$6x$7}
VM=win
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-120}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

src=$REPO/.scratch/present-probes/$ARCH
[ -f "$src/pace.exe" ] || { echo "no probes in $src; run docs/evidence/present/src/build.sh" >&2; exit 125; }
for f in pace.exe gl_alpha.exe; do
    in_lab vmlab cp "$src/$f" "$VM:C:/win3d/present/$ARCH/$f" >/dev/null || exit 125
done
for kv in ${WIN3D_GUEST_ENV:-}; do
    gps "\$u = (Get-ItemProperty 'HKLM:\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon').DefaultUserName; \$s = (New-Object System.Security.Principal.NTAccount(\$u)).Translate([System.Security.Principal.SecurityIdentifier]).Value; New-ItemProperty -Path \"Registry::HKEY_USERS\\\$s\\Environment\" -Name '${kv%%=*}' -Value '${kv#*=}' -Force | Out-Null" >/dev/null
done

name=$CASE$SIZE-$ARCH
gout="C:\\win3d\\present\\$name.txt"
if [ "$CASE" = gl_alpha ]; then
    args="'$SECS','$gout'"; exe="C:\\win3d\\present\\$ARCH\\gl_alpha.exe"
else
    args="'$CASE','$SECS','$gout'${6:+,'$6','$7'}"; exe="C:\\win3d\\present\\$ARCH\\pace.exe"
fi
gps "Remove-Item -Force -ErrorAction SilentlyContinue '$gout'" >/dev/null
in_lab vmlab exec --timeout $((SECS + 300)) "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "& C:\\win3d\\guest\\Run-Probe.ps1 -Path '$exe' -ArgumentList $args -Raw -NoModules -Name present-$name -OutDir C:\\win3d\\results\\present -Seconds $SECS -TimeoutSec $((SECS + 120)); exit \$LASTEXITCODE" \
    >"$OUT/$name.run-probe.out" 2>&1 &
pid=$!
sleep $((SECS / 2 + 8))
timeout 30 bash -c "cd '$LABDIR' && vmlab vm screenshot $VM '$OUT/console-$name.png'" >/dev/null 2>&1
wait "$pid"; rc=$?
in_lab vmlab cp "$VM:C:/win3d/present/$name.txt" "$OUT/$name.txt" >/dev/null 2>&1 || rc=125
for kv in ${WIN3D_GUEST_ENV:-}; do
    gps "\$u = (Get-ItemProperty 'HKLM:\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon').DefaultUserName; \$s = (New-Object System.Security.Principal.NTAccount(\$u)).Translate([System.Security.Principal.SecurityIdentifier]).Value; Remove-ItemProperty -Path \"Registry::HKEY_USERS\\\$s\\Environment\" -Name '${kv%%=*}' -ErrorAction SilentlyContinue" >/dev/null
done
[ -f "$OUT/$name.txt" ] && cat "$OUT/$name.txt"
exit "$rc"
