#!/usr/bin/env bash
# Run single probes, in a given order, in a lab that labs/zeus/run.sh kept
# (--keep): restart the VM (a fresh QEMU and render server), wait for the
# autologon desktop, then run each ARCH-API probe in the user's session and
# print its result line. Used to isolate the D3D12 hang on the RTX 4060
# (docs/zeus.md).
#
#   labs/zeus/probe-order.sh GPU OUTDIR ARCH-API...   e.g. nvidia out x64-vulkan x64-d3d12 x64-vulkan
#
# OUTDIR gets each probe's JSON, the QEMU log lines of this boot, the kernel's
# NVRM lines and the runtime PM transitions.
set -uo pipefail
GPU=$1 OUT=$2; shift 2
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} PATH=$HOME/.local/bin:$PATH
LABDIR=${WIN3D_WORK:-$HOME/win3d-zeus/work}/$GPU/lab
QLOG=${XDG_STATE_HOME:-$HOME/.local/state}/vmlab/labs/win3d-zeus-$GPU/vms/win/qemu.log
PCI=0000:01:00.0; [ "$GPU" = intel ] && PCI=0000:00:02.0
mkdir -p "$OUT"
cd "$LABDIR" || exit 1
gps() { vmlab exec --timeout "${2:-600}" win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "$OUT/order.txt"; }

journalctl -k -f -n 0 -o short-iso --no-pager | grep --line-buffered -E 'NVRM|Xid' >"$OUT/kernel-nvrm.txt" &
JPID=$!
( last=; while :; do c=$(cat /sys/bus/pci/devices/$PCI/power/runtime_status); [ "$c" = "$last" ] || echo "$(date -Is) $c"; last=$c; sleep 1; done ) >"$OUT/runtime-pm.txt" &
PMPID=$!
trap 'kill $JPID $PMPID 2>/dev/null; pkill -f "journalctl -k -f -n 0 -o short-iso"' EXIT

note "restart VM"
vmlab vm stop win && vmlab vm start win || exit 1
QSTART=$(wc -l <"$QLOG" 2>/dev/null || echo 0)
for _ in $(seq 1 60); do
    r=$(gps '$e = Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0; $up = ((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalSeconds; "{0} {1}" -f [bool]$e, [int]$up' 60 2>/dev/null | tail -1)
    case $r in "True "*) [ "${r#True }" -ge 90 ] && break ;; esac
    sleep 10
done
note "desktop: $r"
gps 'Remove-Item -Recurse -Force C:\win3d\order -ErrorAction SilentlyContinue; New-Item -ItemType Directory C:\win3d\order | Out-Null' 60
i=0
for p in "$@"; do
    i=$((i + 1)); arch=${p%%-*} api=${p#*-}
    vmlab exec --timeout 300 win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File 'C:\win3d\guest\Run-Probe.ps1' \
        -Path "C:\\win3d\\probes\\$arch\\${api}_probe.exe" -OutDir 'C:\win3d\order' -Seconds 10 >/dev/null 2>&1
    rc=$?
    j=$(gps "Get-Content C:\\win3d\\order\\$arch-${api}_probe.json -Raw" 60)
    printf '%s\n' "$j" >"$OUT/$i-$p.json"
    note "$i $p: Run-Probe exit $rc; $(printf '%s' "$j" | python3 -c 'import json,sys
try:
    d=json.load(sys.stdin); print(d.get("result"), "fps", d.get("avg_fps"), "readback", (d.get("readback") or {}).get("ok"), "|", "; ".join(d.get("errors") or []))
except Exception as e: print("no json")')"
done
tail -n +"$((QSTART + 1))" "$QLOG" | grep -v '^vmlab: ' | uniq -c >"$OUT/qemu-log.txt"
note "done"
