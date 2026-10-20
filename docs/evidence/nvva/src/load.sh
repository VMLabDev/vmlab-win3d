#!/usr/bin/env bash
# Multi-VM BAR1 load harness for docs/nvva.md.
#
#   load.sh setup <name> [host-gpu] [bundle-dir]  lab win3d-nvva-<name>: up, test desktop, install
#   load.sh load <name> <minutes> <outdir>        probes in a loop until <minutes> pass
#   load.sh monitor <pci-addr> <seconds> <out>    sample the host every 10 s
#   load.sh destroy <name>
#
# Labs live in .scratch/labs/<name>/ in this worktree (from
# labs/probe/vmlab.wcl, whose NAT NIC lets the evaluation template activate;
# setup runs slmgr /ato so wlms.exe does not shut the guest down an hour in).
# `load` runs the ten probes (D3D9, D3D11, D3D12, OpenGL, Vulkan; x64 and
# x86) one after another through tests/guest/Run-Probe.ps1 in the
# autologon desktop session and writes one line per run to <outdir>/runs.txt:
# time, probe, exit code, seconds. `monitor` writes one line per sample:
# time, nvidia-smi BAR1 used MiB, the host kernel's cumulative count of
# "can't alloc VA space" lines, Xid lines, and for every QEMU on the GPU
# the MiB of dma-buf it has mapped (/proc/<pid>/maps). LABS=win3d- watches
# every win3d lab's QEMU, not only this harness's.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
cmd=${1:-}; shift || true

labdir() { echo "$REPO/.scratch/labs/$1"; }
gps() { (cd "$(labdir "$1")" && vmlab exec --timeout "${3:-600}" win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$2") | tr -d '\r'; }
say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
wait_agent() { for _ in $(seq 1 90); do (cd "$(labdir "$1")" && vmlab exec --timeout 20 win -- cmd.exe /c ver) >/dev/null 2>&1 && return 0; sleep 10; done; return 1; }

case $cmd in
setup)
    name=$1; gpu=${2:-Nvidia2}; bundle=${3:-}
    d=$(labdir "$name"); mkdir -p "$d"
    sed -e "s/lab \"win3d-host-probe\"/lab \"win3d-nvva-$name\"/" \
        -e "s/vm \"win11\"/vm \"win\"/" \
        -e "s/host_gpu = \"AMD1\"/host_gpu = \"$gpu\"/" \
        -e "s|^    secure_boot = false|    secure_boot = false\n    agent_update = false|" \
        "$REPO/labs/probe/vmlab.wcl" >"$d/vmlab.wcl"
    grep -q "host_gpu = \"$gpu\"" "$d/vmlab.wcl" || { echo "lab file lacks host_gpu $gpu" >&2; exit 1; }
    (cd "$d" && vmlab up) || exit 1
    wait_agent "$name" || exit 1
    for i in 1 2 3 4 5; do gps "$name" 'New-Item -ItemType Directory -Force C:\win3d\results | Out-Null' 60 >/dev/null && break; sleep 20; done
    # The templates are evaluation editions; unactivated, wlms.exe shuts the
    # guest down about an hour after boot. The probe lab's NAT NIC lets it
    # activate, but a fresh clone does not do it by itself soon enough
    # (docs/known-limits.md), so ask before the install starts the clock.
    slmgr='cscript.exe //nologo C:\Windows\System32\slmgr.vbs'
    (cd "$d" && vmlab exec --timeout 300 win -- cmd.exe /c "$slmgr /ato & $slmgr /xpr") 2>&1 | tr -d '\r' >"$d/activation.txt"
    grep -q 'activated successfully\|permanently activated' "$d/activation.txt" && say "activated" || say "WARNING: Windows did not activate; see $d/activation.txt"
    (cd "$d" && vmlab cp "$REPO/tests/guest" "win:C:/win3d/guest" && vmlab cp "$REPO/out/probes/x64" "win:C:/win3d/probes/x64" && vmlab cp "$REPO/out/probes/x86" "win:C:/win3d/probes/x86") || exit 1
    (cd "$d" && vmlab exec --timeout 1200 win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot) | tr -d '\r' | tail -5
    sleep 60; wait_agent "$name" || exit 1
    "$REPO/scripts/install-into-vm.sh" ${bundle:+--bundle "$bundle"} "$d" win || exit 1
    for _ in $(seq 1 60); do
        r=$(gps "$name" '$e = Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0; $up = ((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalSeconds; "{0} {1}" -f [bool]$e, [int]$up' 60 2>/dev/null | tail -1)
        say "desktop: $r"
        case $r in "True "*) [ "${r#True }" -ge 120 ] && break ;; esac
        sleep 15
    done
    say "setup $name done"
    ;;
load)
    name=$1; minutes=$2; out=$3; mkdir -p "$out"
    end=$(( $(date +%s) + minutes * 60 ))
    while [ "$(date +%s)" -lt "$end" ]; do
        for arch in x64 x86; do for api in d3d11 d3d9 d3d12 opengl vulkan; do
            [ "$(date +%s)" -lt "$end" ] || break 2
            t0=$(date +%s)
            gps "$name" "C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\win3d\\probes\\$arch\\${api}_probe.exe -OutDir C:\\win3d\\results\\probes -Seconds 15 -TimeoutSec 120" 300 >"$out/last-$arch-$api.txt" 2>&1
            rc=$?
            res=$(gps "$name" "(Get-Content C:\\win3d\\results\\probes\\$arch-${api}_probe.json -Raw | ConvertFrom-Json).result" 60 2>/dev/null | tail -1)
            echo "$(date -Is) $name $arch-$api rc=$rc result=${res:-none} $(( $(date +%s) - t0 ))s" | tee -a "$out/runs.txt"
            [ "${res:-none}" = pass ] || cp "$out/last-$arch-$api.txt" "$out/fail-$(date +%H%M%S)-$arch-$api.txt"
        done; done
    done
    ;;
monitor)
    addr=$1; secs=$2; out=$3
    idx=$(nvidia-smi --query-gpu=index,pci.bus_id --format=csv,noheader | awk -F', ' -v a="$addr" 'toupper(substr($2,5))==toupper(a) {print $1}')
    [ -n "$idx" ] || { echo "no NVIDIA GPU at $addr" >&2; exit 1; }
    end=$(( $(date +%s) + secs ))
    while [ "$(date +%s)" -lt "$end" ]; do
        bar1=$(nvidia-smi -i "$idx" -q -d MEMORY | grep -A2 BAR1 | awk '/Used/ {print $3}')
        va=$(journalctl -k -b | grep -c "alloc VA space")
        xid=$(journalctl -k -b | grep -c "Xid")
        q=""
        for p in $(pgrep -f "qemu-system-x86_64 -name vmlab:${LABS:-win3d-nvva}"); do
            env=$(tr '\0' '\n' </proc/$p/environ 2>/dev/null | grep -c "VMLAB_VK_PCI_ADDRESS=$addr")
            [ "$env" = 1 ] || continue
            n=$(tr '\0' ' ' </proc/$p/cmdline | grep -o 'vmlab:[^/]*' | sed 's/vmlab:win3d-//')
            mb=$(awk '/dmabuf/ {split($1,a,"-"); s+=strtonum("0x"a[2])-strtonum("0x"a[1])} END {printf "%d", s/1048576}' /proc/$p/maps 2>/dev/null)
            q="$q $n:${mb}MiB"
        done
        echo "$(date -Is) bar1_used=${bar1}MiB va_lines=$va xid_lines=$xid qemu_dmabuf=[${q# }]" >>"$out"
        sleep 10
    done
    ;;
destroy)
    (cd "$(labdir "$1")" && vmlab destroy)
    ;;
*) sed -n '2,18p' "$0"; exit 2 ;;
esac
