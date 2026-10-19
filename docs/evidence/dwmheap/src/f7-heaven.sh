#!/usr/bin/env bash
# compat finding F7 against this branch's build: five Unigine Heaven D3D11
# runs on AMD1 with full PageHeap and WER dumps for Heaven.exe, the way
# worker unid3d reproduced it (docs/unid3d.md, 2 crashes in 5 runs).
#
#   docs/evidence/dwmheap/src/f7-heaven.sh <evidence-dir> [runs]
#
# Uses tests/apps/compat/run.sh (lab win3d-dwmheap-f7-amd1, package from
# out/package/), src/pageheap.sh, and after each run lists C:\win3d\dumps.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
EV=$(mkdir -p "$1" && cd "$1" && pwd) RUNS=${2:-5}
export COMPAT_LAB=win3d-dwmheap-f7-amd1
LABDIR=$REPO/.scratch/compat/$COMPAT_LAB/lab
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
COMPAT_EV=$EV/setup "$REPO/tests/apps/compat/run.sh" --host-gpu AMD1 --only heaven-d3d11 --lab-only || exit 1
"$REPO/docs/evidence/dwmheap/src/pageheap.sh" "$LABDIR" win Heaven.exe on | tee "$EV/pageheap.txt"
for n in $(seq 1 "$RUNS"); do
    note "Heaven D3D11 run $n"
    COMPAT_EV=$EV/try$n "$REPO/tests/apps/compat/run.sh" --host-gpu AMD1 --reuse --keep --only heaven-d3d11 || true
    (cd "$LABDIR" && vmlab exec --timeout 60 win -- powershell.exe -NoProfile -Command \
        'Get-ChildItem C:\win3d\dumps -ErrorAction SilentlyContinue | % { "{0} {1} {2}" -f $_.Name, $_.Length, $_.LastWriteTime.ToString("s") }' | tr -d '\r') >"$EV/try$n/dumps.txt"
    note "dumps after run $n:"; cat "$EV/try$n/dumps.txt"
done
(cd "$LABDIR" && vmlab destroy) || true
