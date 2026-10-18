#!/usr/bin/env bash
# Sample an NVIDIA card's BAR1 use every 2 s until killed.
#   docs/evidence/unigl/src/bar1-sample.sh PCI-BUS-ID OUT
# PCI-BUS-ID as nvidia-smi prints it (00000000:01:00.0 for Nvidia1).
set -uo pipefail
id=$1 out=$2
echo "# time bar1_used_mib bar1_total_mib ($id)" >"$out"
while :; do
    nvidia-smi -i "$id" -q -d MEMORY | awk -v t="$(date +%T)" '
        /BAR1 Memory Usage/ { b = 1 } b && /Total/ { tot = $3 } b && /Used/ { print t, $3, tot; exit }' >>"$out"
    sleep 2
done
