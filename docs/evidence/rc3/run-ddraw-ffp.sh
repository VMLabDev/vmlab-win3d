#!/usr/bin/env bash
# The rc3 DirectDraw fullscreen and D3D9 fixed-function (ffp, ffp2) runs, in a lab kept
# by `tests/e2e/run.sh --keep` (the package already installed, the autologon
# desktop up). Builds nothing: out/ddraw/ (docs/evidence/ddraw/src/build.sh),
# .scratch/ffp-probes/ (docs/evidence/ffp/src/build.sh) and
# .scratch/ffp2-probes/ (docs/evidence/ffp2/src/build.sh) must exist.
#
#   docs/evidence/rc3/run-ddraw-ffp.sh <lab-dir> <gpu dir name, e.g. amd1>
#
# Writes docs/evidence/rc3/ddraw/<gpu>/<run>/ (run-guest.sh output; one
# console capture per run is kept), docs/evidence/rc3/ffp/<gpu>/
# (ffp's run-repro.sh output) and docs/evidence/rc3/ffp2/<gpu>/ (ffp2's).
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
LAB=$1 GPU=$2
DD=$REPO/docs/evidence/rc3/ddraw/$GPU
FFP=$REPO/docs/evidence/rc3/ffp/$GPU
FFP2=$REPO/docs/evidence/rc3/ffp2/$GPU
mkdir -p "$DD" "$FFP" "$FFP2"
run() { # run NAME ARCH ARGS...: one ddfull run into $DD/NAME
    local name=$1 arch=$2; shift 2
    local out=$DD/$name
    rm -rf "$out"; mkdir -p "$out"
    "$REPO/docs/evidence/ddraw/run-guest.sh" "$LAB" "$out" "$arch" ddfull 1 "$@"   # --seconds 1: the frame count decides; Run-Probe times out at 301 s
    # Keep one capture from the middle of the run.
    local shots=("$out"/console-*.png) keep
    if [ -e "${shots[0]}" ]; then
        keep=${shots[$(( ${#shots[@]} / 2 ))]}
        mv "$keep" "$out/console-mid.png"
        find "$out" -name 'console-ddfull-*.png' -delete
    fi
}
run x64-rate  x64 --modes 640x480x32,800x600x32,1024x768x32 --frames 120 --check-every 30
run x64-full  x64 --modes 800x600x32,640x480x16 --frames 300 --check-every 1
run x86-32and16 x86 --modes 800x600x32,640x480x16 --frames 300 --check-every 1
run x86-16bpp x86 --modes 640x480x16,800x600x16 --frames 300 --check-every 1
(cd "$LAB" && vmlab status -v) >"$DD/status-v.txt" 2>&1
"$REPO/docs/evidence/ffp/src/run-repro.sh" "$LAB" win "$FFP"
echo "ffp exit $?"
"$REPO/docs/evidence/ffp2/src/run-repro.sh" "$LAB" win "$FFP2"
echo "ffp2 exit $?"
