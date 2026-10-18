#!/usr/bin/env bash
# Run single probes with VN_PERF (Mesa Venus performance options) set for the
# probe process only, through a .cmd wrapper, in the NVIDIA lab that
# labs/zeus/run.sh --gpu nvidia --keep left running. Prints one result line
# per probe and keeps each probe's JSON in OUT. docs/zeus.md, "RTX 4060".
#
#   labs/zeus/envprobe.sh OUT TAG VN_PERF ARCH-API...
#   e.g. labs/zeus/envprobe.sh ~/win3d-zeus/env nofb no_fence_feedback,no_semaphore_feedback x64-vulkan x64-d3d12
set -uo pipefail
OUT=$1 TAG=$2 PERF=$3; shift 3
export XDG_RUNTIME_DIR=/run/user/$(id -u) PATH=$HOME/.local/bin:$PATH
cd ~/win3d-zeus/work/nvidia/lab
mkdir -p $OUT
gps() { vmlab exec --timeout 120 win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
for p in "$@"; do
  arch=${p%%-*} api=${p#*-} n=$TAG-$p
  gps "New-Item -ItemType Directory -Force C:\\win3d\\env | Out-Null; Set-Content -Encoding ascii C:\\win3d\\env\\$n.cmd -Value @('@set VN_PERF=$PERF', '@C:\\win3d\\probes\\$arch\\${api}_probe.exe --seconds 10 --out C:\\win3d\\env\\$n.json')"
  vmlab exec --timeout 300 win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File 'C:\win3d\guest\Run-Probe.ps1' -Path "C:\\win3d\\env\\$n.cmd" -Raw -NoModules -Name $n -OutDir 'C:\win3d\env' >/dev/null 2>&1
  j=$(gps "Get-Content C:\\win3d\\env\\$n.json -Raw")
  printf '%s\n' "$j" > $OUT/$n.json
  echo "$n (VN_PERF=$PERF): $(printf '%s' "$j" | python3 -c 'import json,sys
try:
    d=json.load(sys.stdin); print(d.get("result"), "fps", d.get("avg_fps"), "frames", d.get("frames_presented"), "readback", (d.get("readback") or {}).get("ok"), "|", "; ".join(d.get("errors") or []))
except Exception as e: print("no json")')" | tee -a $OUT/env.txt
done
