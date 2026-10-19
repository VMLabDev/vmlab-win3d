#!/usr/bin/env bash
# Host CPU contention on chosen CPUs only: N busy loops pinned to a CPU list,
# for SECONDS (default 3600). Used with pin.sh, so the VM under test shares
# those CPUs with the loops and nothing else on the host is slowed.
#
#   burn.sh <cpu-list> <N> [SECONDS]
set -euo pipefail
cpus=$1 n=$2 secs=${3:-3600}
for _ in $(seq 1 "$n"); do
    taskset -c "$cpus" timeout "$secs" sh -c 'while :; do :; done' &
done
echo "burning $n loops on CPUs $cpus for $secs s"
wait
