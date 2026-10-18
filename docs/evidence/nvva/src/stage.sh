#!/usr/bin/env bash
# One load stage of the docs/nvva.md repro: probe loops in the named labs at
# once for <minutes>, optionally with a host process holding BAR1 (bar1probe)
# as a stand-in for one more tenant on the card, then a summary.
#
#   stage.sh <outdir> <minutes> <hog-MiB|0> <lab> [lab...]
#
# Needs load.sh monitor running for the card (its file is <monitor>, given
# in MONITOR=). Writes <outdir>/<lab>/runs.txt and <outdir>/summary.txt:
# probe runs and failures per lab, new "can't alloc VA space" lines, and the
# highest BAR1 use that MONITOR saw during the stage.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
out=$1 minutes=$2 hog=$3; shift 3
mkdir -p "$out"
va0=$(journalctl -k -b | grep -c "alloc VA space")
t0=$(date -Is)
hogpid=
if [ "$hog" != 0 ]; then
    cc -O2 -o "$out/bar1probe" "$HERE/bar1probe.c" -lvulkan
    VK_LOADER_DRIVERS_SELECT='*nvidia*' "$out/bar1probe" 05 "$hog" "$hog" dmabuf $(( minutes * 60 + 30 )) >"$out/hog.txt" 2>&1 &
    hogpid=$!
fi
for lab in "$@"; do "$HERE/load.sh" load "$lab" "$minutes" "$out/$lab" >/dev/null 2>&1 & done
wait $(jobs -p | grep -v "^${hogpid:-x}$")
[ -n "$hogpid" ] && kill "$hogpid" 2>/dev/null; wait 2>/dev/null
rm -f "$out/bar1probe"
va1=$(journalctl -k -b | grep -c "alloc VA space")
{
    echo "stage: $(basename "$out")  labs: $*  minutes: $minutes  host hog: ${hog} MiB (dma-buf mapped, so about twice that in BAR1)"
    echo "from $t0 to $(date -Is)"
    for lab in "$@"; do
        f=$out/$lab/runs.txt
        echo "$lab: runs $(wc -l <"$f" 2>/dev/null || echo 0), pass $(grep -c 'result=pass' "$f" 2>/dev/null), not pass $(grep -vc 'result=pass' "$f" 2>/dev/null)"
        grep -v 'result=pass' "$f" 2>/dev/null | sed 's/^/  /'
    done
    echo "new 'can't alloc VA space' kernel lines: $((va1 - va0))"
    [ -n "${MONITOR:-}" ] && awk -v a="$t0" '$1 >= a { sub(/bar1_used=/, "", $2); sub(/MiB/, "", $2); if ($2 + 0 > m) m = $2 + 0 } END { print "highest BAR1 used: " m " MiB of 256" }' "$MONITOR"
    since=${t0%+*}; journalctl -k -b --since "${since/T/ }" | grep -E "Xid" | sed 's/^/  /'
} | tee "$out/summary.txt"
