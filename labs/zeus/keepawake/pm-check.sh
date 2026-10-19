#!/usr/bin/env bash
# Show what keepawake does to the GPU's runtime power state: sample
# runtime_status once a second with no client, with keepawake --hold-only,
# with no client again, and with keepawake clearing every 100 ms.
#
#   labs/zeus/keepawake/pm-check.sh [PCI] [SECONDS]   (default 0000:01:00.0, 40)
#
# Prints one line per phase: the samples, then the counts per state. Build
# keepawake first (build.sh next to this script).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PCI=${1:-0000:01:00.0}
N=${2:-40}
P=/sys/bus/pci/devices/$PCI/power
samp() { # samp NAME: N one-second samples of runtime_status
    local s=() i
    for i in $(seq 1 "$N"); do s+=("$(cat "$P/runtime_status")"); sleep 1; done
    printf '%-10s %s\n           counts: %s\n' "$1" "${s[*]}" "$(printf '%s\n' "${s[@]}" | sort | uniq -c | tr -s ' \n' ' ')"
}
echo "date: $(date -Is)"
echo "device: $PCI $(lspci -s "$PCI" | cut -d' ' -f2-)"
echo "power/control: $(cat "$P/control")"
grep -E 'Runtime D3|Video Memory:' "/proc/driver/nvidia/gpus/$PCI/power" 2>/dev/null
grep -E 'DynamicPowerManagement' /proc/driver/nvidia/params 2>/dev/null
samp idle
"$HERE/keepawake" --pci "$PCI" --hold-only & k=$!; sleep 1
samp hold-only
kill "$k"; wait "$k"
sleep 5
samp idle
"$HERE/keepawake" --pci "$PCI" & k=$!; sleep 1
samp clearing
kill "$k"; wait "$k"
samp idle
