#!/usr/bin/env bash
# Sample a guest program's address-space use every N seconds with
# vasample.exe (built by build.sh) until it exits or --max seconds pass.
# One line per sample: t=<s since start> followed by vasample's fields.
#
#   docs/evidence/f6va/src/sample-va.sh <lab-dir> <vm> <process-name> [interval] [max]
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
lab=$(cd "$1" && pwd) vm=$2 proc=$3 iv=${4:-10} max=${5:-900}
cd "$lab"
vmlab cp "$repo/.scratch/f6va-probes/x64/vasample.exe" "$vm:C:/win3d/vasample.exe" >/dev/null || exit 1
t0=$(date +%s) seen=0
while [ $(( $(date +%s) - t0 )) -lt "$max" ]; do
    g=$(vmlab exec --timeout 30 "$vm" -- 'C:\win3d\vasample.exe' "$proc" 2>/dev/null | tr -d '\r' | tail -1)
    if [ "$g" = none ] || [ -z "$g" ]; then
        [ "$seen" = 1 ] && { echo "t=$(( $(date +%s) - t0 )) exited"; break; }
    else
        seen=1
        echo "t=$(( $(date +%s) - t0 )) $g"
    fi
    sleep "$iv"
done
