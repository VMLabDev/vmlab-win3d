#!/usr/bin/env bash
# Starve one lab's VM of host CPU: pin its QEMU process and the Venus render
# server (and every render-server child, which new contexts inherit) to a
# CPU list, or back to all CPUs.
#
#   pin.sh <lab-name> <cpu-list>     e.g. pin.sh win3d-x86d3d12-amd1 30,31
#   pin.sh <lab-name> all
set -euo pipefail
lab=$1 cpus=$2
[ "$cpus" = all ] && cpus=0-$(($(nproc --all) - 1))
qemu=$(pgrep -f "^qemu-system-x86_64 -name vmlab:$lab/" | head -1)
[ -n "$qemu" ] || { echo "no QEMU for $lab" >&2; exit 1; }
pids=$(pstree -p "$qemu" | grep -o "([0-9]*)" | tr -d "()" | sort -u)
for p in $pids; do taskset -a -cp "$cpus" "$p" >/dev/null 2>&1 || true; done
echo "pinned $(echo "$pids" | wc -w) processes of $lab (QEMU $qemu) to CPUs $cpus"
