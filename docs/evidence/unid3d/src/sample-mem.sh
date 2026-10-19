#!/usr/bin/env bash
# Sample the memory of a running guest program and of the lab's QEMU and
# render-server processes every N seconds until the program exits (or
# --max seconds pass). One line per sample:
#   t=<s> guest_pm=<private MiB> guest_ws=<working set MiB> guest_handles=<n>
#   guest_va=<virtual size MiB>
#   qemu_rss=<MiB> rs_rss=<MiB, all render-server processes>
#   rs_maps=<mappings> rs_dmabuf=<dma-buf fds>
#
#   docs/evidence/unid3d/src/sample-mem.sh <lab-dir> <vm> <lab-name> <process-name> [interval] [max]
set -uo pipefail
lab=$(cd "$1" && pwd) vm=$2 labname=$3 proc=$4 iv=${5:-10} max=${6:-600}
cd "$lab"
t0=$(date +%s) seen=0
while [ $(( $(date +%s) - t0 )) -lt "$max" ]; do
    qpid=$(pgrep -f "^qemu-system-x86_64 -name vmlab:$labname/$vm" | head -1)
    rs_rss=0 rs_maps=0 rs_dmabuf=0 qemu_rss=0
    if [ -n "$qpid" ]; then
        qemu_rss=$(awk '/VmRSS/ {print int($2/1024)}' /proc/$qpid/status 2>/dev/null)
        for p in $(pgrep -f virgl_render_server); do
            # Only this lab's render servers: descendants of its QEMU.
            a=$p; while [ "$a" -gt 1 ] && [ "$a" != "$qpid" ]; do a=$(awk '{print $4}' /proc/$a/stat 2>/dev/null || echo 1); done
            [ "$a" = "$qpid" ] || continue
            rs_rss=$(( rs_rss + $(awk '/VmRSS/ {print int($2/1024)}' /proc/$p/status 2>/dev/null || echo 0) ))
            rs_maps=$(( rs_maps + $(wc -l </proc/$p/maps 2>/dev/null || echo 0) ))
            rs_dmabuf=$(( rs_dmabuf + $(ls -l /proc/$p/fd 2>/dev/null | grep -c dmabuf) ))
        done
    fi
    g=$(vmlab exec --timeout 30 "$vm" -- powershell.exe -NoProfile -NonInteractive -Command "\$p = Get-Process $proc -ErrorAction SilentlyContinue | Select-Object -First 1; if (\$p) { '{0} {1} {2} {3}' -f [int](\$p.PrivateMemorySize64/1MB), [int](\$p.WorkingSet64/1MB), \$p.HandleCount, [int64](\$p.VirtualMemorySize64/1MB) } else { 'none' }" 2>/dev/null | tr -d '\r' | tail -1)
    if [ "$g" = none ] || [ -z "$g" ]; then
        [ "$seen" = 1 ] && { echo "t=$(( $(date +%s) - t0 )) exited"; break; }
    else
        seen=1
        set -- $g
        echo "t=$(( $(date +%s) - t0 )) guest_pm=$1 guest_ws=$2 guest_handles=$3 guest_va=$4 qemu_rss=$qemu_rss rs_rss=$rs_rss rs_maps=$rs_maps rs_dmabuf=$rs_dmabuf"
    fi
    sleep "$iv"
done
