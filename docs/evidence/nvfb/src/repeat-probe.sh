#!/usr/bin/env bash
# Run one probe N times in a kept lab, with extra environment variables for
# the probe process only (a .cmd wrapper, as labs/zeus/envprobe.sh does), and
# count readback failures. For the intermittent black OpenGL readback
# (docs/nvfb.md).
#
#   docs/evidence/nvfb/src/repeat-probe.sh <lab-dir> <out-dir> <tag> <arch-api> <n> [NAME=VALUE ...]
#   e.g. repeat-probe.sh ~/win3d-nvfb/e2e/.scratch/e2e/windows-11-nvidia1/lab out base x86-opengl 8
set -uo pipefail
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} PATH=$HOME/.local/bin:$PATH
LAB=$1 OUTD=$2 TAG=$3 P=$4 N=$5; shift 5
mkdir -p "$OUTD"; OUT=$(cd "$OUTD" && pwd)
cd "$LAB"
arch=${P%%-*} api=${P#*-}
gps() { vmlab exec --timeout 120 win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
sets=
for kv in "$@"; do sets="$sets'@set $kv', "; done
fail=0
for i in $(seq 1 "$N"); do
    n=$TAG-$P-$i
    gps "New-Item -ItemType Directory -Force C:\\win3d\\rep | Out-Null; Set-Content -Encoding ascii C:\\win3d\\rep\\$n.cmd -Value @($sets'@C:\\win3d\\probes\\$arch\\${api}_probe.exe --seconds 8 --out C:\\win3d\\rep\\$n.json')"
    vmlab exec --timeout 300 win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File 'C:\win3d\guest\Run-Probe.ps1' -Path "C:\\win3d\\rep\\$n.cmd" -Raw -NoModules -Name $n -OutDir 'C:\win3d\rep' >/dev/null 2>&1
    gps "Get-Content C:\\win3d\\rep\\$n.json -Raw" >"$OUT/$n.json"
    r=$(python3 -c 'import json,sys
try:
    d=json.load(open(sys.argv[1])); rb=d.get("readback") or {}
    print(d.get("result"), "fps", d.get("avg_fps"), "readback", rb.get("ok"), [p.get("actual") for p in rb.get("pixels", [])])
except Exception: print("no json")' "$OUT/$n.json")
    case $r in pass*) ;; *) fail=$((fail + 1)) ;; esac
    echo "$n ($*): $r" | tee -a "$OUT/repeat.txt"
done
echo "$TAG $P: $fail of $N failed ($*)" | tee -a "$OUT/repeat.txt"
