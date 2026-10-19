#!/usr/bin/env bash
# Loop the D3D12 probe (or the phase repro) in a running lab, with host and
# guest timestamps, and record every run.
#
#   loop.sh [options] <lab-dir> <vm> <out-dir>
#     -n N         iterations (default 50)
#     -a "x86 x64" architectures run in each iteration, in order
#     -d DIR       guest directory with <arch>\d3d12_probe.exe
#                  (default C:\win3d\probes; the repro is copied to C:\win3d\phases)
#     -s SECONDS   --seconds for the probe (default 20, as tests/e2e/run.sh)
#     -w           wipe the vkd3d-proton shader cache before every run
#     -c           take console captures every 2 s while a run is going (as run.sh)
#
# Each run goes through tests/guest/Run-Probe.ps1 (already in C:\win3d\guest),
# so it runs in the autologon user's session like the e2e probes. Writes
# <out-dir>/runs.tsv (one line per run: host start time, host load average,
# arch, Run-Probe duration, the probe's elapsed_seconds and frames) and
# each run's probe JSON (and the repro's phase trace) next to it.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
N=50 ARCHS="x86 x64" DIR='C:\win3d\probes' SECS=20 WIPE=0 CAP=0
while getopts n:a:d:s:wc o; do
    case $o in
        n) N=$OPTARG ;; a) ARCHS=$OPTARG ;; d) DIR=$OPTARG ;; s) SECS=$OPTARG ;;
        w) WIPE=1 ;; c) CAP=1 ;; *) sed -n '2,20p' "$0" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 3 ] || { sed -n '2,20p' "$0" >&2; exit 2; }
labdir=$(cd "$1" && pwd) vm=$2 out=$3
mkdir -p "$out/shots"
out=$(cd "$out" && pwd)
gdir="C:\\win3d\\x86d3d12\\$(basename "$out")"
in_lab() { (cd "$labdir" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-120}" "$vm" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }

gps "New-Item -ItemType Directory -Force '$gdir' | Out-Null" >/dev/null
[ -f "$out/runs.tsv" ] || printf 'iter\tarch\thost_start\thost_load1\trc\trun_s\telapsed_s\tframes\tfps\tresult\twindow_frames\n' >"$out/runs.tsv"
for i in $(seq 1 "$N"); do
    for a in $ARCHS; do
        name=$a-$(printf %03d "$i")
        if [ "$WIPE" = 1 ]; then
            gps 'Get-ChildItem C:\Users\*\AppData\Local\vkd3d-proton -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue' >/dev/null
        fi
        start=$(date -Is) load=$(cut -d' ' -f1 /proc/loadavg)
        in_lab vmlab exec --timeout 900 "$vm" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
            -File 'C:\win3d\guest\Run-Probe.ps1' -Path "$DIR\\$a\\d3d12_probe.exe" -Name "$name" \
            -OutDir "$gdir" -Seconds "$SECS" >"$out/$name.out" 2>&1 &
        pid=$! n=0
        if [ "$CAP" = 1 ]; then
            while kill -0 "$pid" 2>/dev/null; do
                sleep 2; n=$((n + 1))
                f=$out/shots/$name-$(printf %03d $n).png
                in_lab vmlab vm screenshot "$vm" "$f" >/dev/null 2>&1 &&
                    python3 "$repo/tests/e2e/findprobe.py" "$f" >"${f%.png}.json" 2>/dev/null
                rm -f "$f"
            done
        fi
        wait "$pid"; rc=$?
        gps "Get-Content -Raw '$gdir\\$name.json'" 60 >"$out/$name.json"
        gps "if (Test-Path '$gdir\\$name.json.phases.txt') { Get-Content -Raw '$gdir\\$name.json.phases.txt' }" 60 >"$out/$name.phases.txt"
        [ -s "$out/$name.phases.txt" ] || rm -f "$out/$name.phases.txt"
        win=-
        [ "$CAP" = 1 ] && win=$(cat "$out"/shots/"$name"-*.json 2>/dev/null | grep -c '"probe_window": {')
        python3 - "$i" "$a" "$start" "$load" "$rc" "$win" "$out/runs.tsv" "$out/$name.out" "$out/$name.json" <<'PY'
import json, sys, re
i, a, start, load, rc, win, tsv, outf, jf = sys.argv[1:10]
try:
    d = json.load(open(jf, encoding='utf-8-sig'))
except Exception:
    d = {}
run_s = '-'
m = re.search(r'"duration_s"\s*:\s*([0-9.]+)', open(outf, errors='replace').read())
if m: run_s = m.group(1)
row = [i, a, start, load, rc, run_s, d.get('elapsed_seconds', '-'), d.get('frames_presented', '-'),
       d.get('avg_fps', '-'), d.get('result', '-'), win]
open(tsv, 'a').write('\t'.join(map(str, row)) + '\n')
print('\t'.join(map(str, row)), flush=True)
PY
    done
done
