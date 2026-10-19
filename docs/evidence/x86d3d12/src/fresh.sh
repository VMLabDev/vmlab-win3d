#!/usr/bin/env bash
# One fresh-lab acceptance run with the phase-traced repro in place of the
# D3D12 probes, so a stall in the first D3D12 run of a new lab leaves a phase
# trace and stack scans.
#
#   [FIRSTLOAD=1] [KEEP=1] fresh.sh <host-gpu> <lab-name> <out-dir>
#
# Puts out/x86d3d12/<arch>/d3d12_phases.exe (src/build.sh) in out/probes as
# d3d12_probe.exe for both arches, runs tests/e2e/run.sh --keep, then saves
# results.txt, the probe JSONs, the phase traces, the Defender log lines of the
# run and the run log to <out-dir>, restores out/probes and the tracked
# acceptance evidence, and destroys the lab (KEEP=1 leaves it running). The
# GPU slot is taken with .scratch/workers/gpuslot.sh before the lab comes up
# and given back after it is destroyed.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
gpu=$1 lab=$2 out=$3
mkdir -p "$out"; out=$(cd "$out" && pwd)
suffix=; [ "$gpu" = AMD1 ] || suffix=-${gpu,,}
work=$repo/.scratch/e2e/windows-11$suffix ev=$repo/docs/evidence/acceptance/windows-11$suffix
for a in x64 x86; do
    cp -n "$repo/out/probes/$a/d3d12_probe.exe" "$repo/out/probes/$a/d3d12_probe.exe.orig"
    cp "$repo/out/x86d3d12/$a/d3d12_phases.exe" "$repo/out/probes/$a/d3d12_probe.exe"
done
restore() {
    for a in x64 x86; do
        [ -f "$repo/out/probes/$a/d3d12_probe.exe.orig" ] &&
            mv "$repo/out/probes/$a/d3d12_probe.exe.orig" "$repo/out/probes/$a/d3d12_probe.exe"
    done
}
trap restore EXIT
slot=/home/wil/dev/vmlab-win3d/.scratch/workers/gpuslot.sh
"$slot" acquire "$gpu" x86d3d12
rm -f "$work/run.log"
if [ "${FIRSTLOAD:-0}" = 1 ]; then
    # Once the install is done (run.sh's step 5 starts), time the first and
    # second image loads of the D3D12 DLLs (Test-FirstLoad.ps1), before any
    # D3D12 probe has loaded them.
    (
        log=$work/run.log
        for _ in $(seq 1 720); do grep -q '== 5 interactive-session tests' "$log" 2>/dev/null && break; sleep 5; done
        cd "$work/lab" && vmlab cp "$here/Test-FirstLoad.ps1" win:C:/win3d/Test-FirstLoad.ps1 >/dev/null &&
            vmlab exec --timeout 900 win -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
                -File 'C:\win3d\Test-FirstLoad.ps1' 2>&1 | tr -d '\r' >"$out/firstload.txt"
    ) &
fi
"$repo/tests/e2e/run.sh" --template windows-11 --host-gpu "$gpu" --lab "$lab" --keep
echo "run.sh exit $?" >"$out/exit.txt"
cp "$ev/results.txt" "$ev/run.log" "$ev/host.txt" "$out/" 2>/dev/null
mkdir -p "$out/probes"
cp "$ev"/probes/*_probe.json "$ev"/probes/*_probe.run.json "$out/probes/" 2>/dev/null
cp "$work"/results/probes/*.phases.txt "$work"/results/probes/*_dxgi.log "$out/probes/" 2>/dev/null
cp "$ev/shots.json" "$out/" 2>/dev/null
(cd "$work/lab" && vmlab exec --timeout 120 win -- powershell.exe -NoProfile -Command \
    '$f = Get-ChildItem "C:\ProgramData\Microsoft\Windows Defender\Support\MPLog-*.log" | Sort LastWriteTime | Select -Last 1; Get-Content $f.FullName | Select-String -Pattern "probe|vkd3d|vmlab-win3d|expensive|caching disabled|Cloud\] (Start|End)|Dynamic signature|ProcessImageName" | % { $_.Line }' \
    2>&1 | tr -d '\r' | grep '^20' >"$out/defender-mplog.txt")
grep -av '^vmlab' "${XDG_STATE_HOME:-$HOME/.local/state}/vmlab/labs/$lab/vms/win/qemu.log" | sort | uniq -c | sort -rn | head -40 >"$out/qemu-log-summary.txt" 2>/dev/null
if [ "${KEEP:-0}" = 1 ]; then
    echo "KEEP=1: $lab left running; destroy it, then: $slot release $gpu x86d3d12"
else
    (cd "$work/lab" && vmlab destroy) && "$slot" release "$gpu" x86d3d12
fi
git -C "$repo" checkout -q -- "$ev" 2>/dev/null; git -C "$repo" clean -fdq -- "$ev"
tail -3 "$out/results.txt"
