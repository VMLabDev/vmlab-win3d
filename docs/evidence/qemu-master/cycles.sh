#!/usr/bin/env bash
# Stop and start a VM of an installed e2e lab N times, with Venus work before
# each stop, and record how each QEMU exited.
#
#   cycles.sh LABDIR LAB N OUTDIR
#
# Run under the supervisor that owns the lab (source env.sh first). Each
# cycle: wait for the agent and the autologon desktop, run the x64 D3D12 and
# D3D11 probes for 15 s each, take a console screenshot, `vmlab vm stop`,
# then read the lab's vm.stopped event and coredumpctl for that QEMU pid.
set -uo pipefail
LABDIR=$1 LAB=$2 N=$3 OUT=$4 VM=win
EVENTS=${XDG_STATE_HOME:-$HOME/.local/state}/vmlab/labs/$LAB/events.jsonl
mkdir -p "$OUT"
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-120}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
log() { printf '[%s] %s\n' "$(date +%T)" "$*" | tee -a "$OUT/cycles.log"; }
qpid() { pgrep -f "qemu-system-x86_64 -name vmlab:$LAB/$VM " | head -1; }

for i in $(seq 1 "$N"); do
    for _ in $(seq 1 90); do in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && break; sleep 10; done
    for _ in $(seq 1 40); do
        r=$(gps '$e = Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0; $up = ((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalSeconds; "{0} {1}" -f [bool]$e, [int]$up' 60 2>/dev/null | tail -1)
        case $r in "True "*) [ "${r#True }" -ge 60 ] && break ;; esac
        sleep 10
    done
    pid=$(qpid)
    log "cycle $i: qemu pid $pid, desktop '$r'"
    for api in d3d12 d3d11; do
        gps "& C:\\win3d\\guest\\Run-Probe.ps1 -Path C:\\win3d\\probes\\x64\\${api}_probe.exe -OutDir C:\\win3d\\results\\cycles -Seconds 15" 300 \
            >"$OUT/cycle-$i-$api.out" 2>&1 || true
        log "  $api probe: $(grep -iE 'exit|pass|fail' "$OUT/cycle-$i-$api.out" | tail -1)"
    done
    in_lab vmlab vm screenshot "$VM" "$OUT/cycle-$i.png" >/dev/null 2>&1 && log "  screenshot $(magick "$OUT/cycle-$i.png" -format '%[fx:mean]' info: 2>/dev/null)"
    t0=$(date -Is)
    in_lab vmlab vm stop "$VM" >"$OUT/cycle-$i-stop.out" 2>&1; rc=$?
    sleep 5
    st=$(grep '"vm.stopped"' "$EVENTS" | tail -1)
    cores=$(coredumpctl list --no-legend --since "$t0" 2>/dev/null | grep -c "qemu-system" || true)
    pidcore=$(coredumpctl list --no-legend --since "$t0" 2>/dev/null | awk -v p="$pid" '$5 == p' | wc -l)
    log "  stop rc $rc; event: $st; qemu cores since stop (any pid) $cores, this pid $pidcore"
    [ "$i" -lt "$N" ] && { in_lab vmlab vm start "$VM" >"$OUT/cycle-$i-start.out" 2>&1 || log "  start failed: $(tail -2 "$OUT/cycle-$i-start.out")"; }
done
log "done: $N cycles"
grep -c 'SIGSEGV' "$OUT/cycles.log" | xargs -I{} echo "SIGSEGV lines: {}" | tee -a "$OUT/cycles.log"
