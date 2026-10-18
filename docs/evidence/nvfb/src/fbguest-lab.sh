#!/usr/bin/env bash
# Bring up a Windows 11 lab on one host GPU, install a vmlab-win3d bundle and
# run fbguest.exe (guest-fbtest/build.sh) in it, x64 and x86, in the
# autologon user's session. Runs on the host that owns the GPU, from a
# directory laid out as this repo (out/nvfb/guest, scripts/install-into-vm.sh,
# tests/guest).
#
#   docs/evidence/nvfb/src/fbguest-lab.sh <host_gpu> <bundle-dir> <out-dir> [--keep]
#
# The lab is win3d-nvfb-<host_gpu, lower case>, in .scratch/fbguest-<gpu>/;
# it is destroyed at the end unless --keep (then FBGUEST_REUSE=1 runs the
# tests again in it without a new install).
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
export PATH=$HOME/.local/bin:$PATH
GPU=$1 BUNDLE=$(cd "$2" && pwd) KEEP=${4:-}
mkdir -p "$3"; OUT=$(cd "$3" && pwd)
LAB=win3d-nvfb-${GPU,,}
LABDIR=$REPO/.scratch/fbguest-${GPU,,}
VM=win
mkdir -p "$LABDIR"
cd "$LABDIR"
gps() { vmlab exec --timeout "${2:-300}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

if [ "${FBGUEST_REUSE:-0}" != 1 ]; then
    [ -f vmlab.wcl ] && vmlab destroy >/dev/null 2>&1
    cat >vmlab.wcl <<EOF
import <vmlab.wcl>

// fbguest lab (docs/nvfb.md): labs/probe/vmlab.wcl on host GPU $GPU.
lab "$LAB" {
  vm "$VM" {
    template = "x86_64/windows-11@26100.1742.5"
    secure_boot = false
    agent_update = false
    nic { nat = true }
    gpu {
      mode     = "vulkan"
      host_gpu = "$GPU"
    }
  }
}
EOF
    say "up $LAB"
    vmlab up || exit 1
    for _ in $(seq 1 90); do
        vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && break
        sleep 10
    done
    # The autologon desktop the probes run in (as tests/e2e/run.sh does).
    vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest" >/dev/null
    vmlab exec --timeout 1200 "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
        -File 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >"$OUT/test-desktop.txt" 2>&1
    sleep 60
    for _ in $(seq 1 90); do
        vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && break
        sleep 10
    done
    say "install $(basename "$BUNDLE")"
    "$REPO/scripts/install-into-vm.sh" --bundle "$BUNDLE" "$LABDIR" "$VM" >"$OUT/install.log" 2>&1 ||
        { say "install failed"; tail -20 "$OUT/install.log"; exit 1; }
    for _ in $(seq 1 90); do
        vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && break
        sleep 10
    done
fi
vmlab cp "$REPO/out/nvfb/guest" "$VM:C:/win3d/fbguest" >/dev/null
gps 'New-Item -ItemType Directory -Force C:\win3d\results\fbguest | Out-Null; Get-ChildItem Env: | Where-Object Name -like "VN_*" | Format-Table -HideTableHeaders | Out-String' >"$OUT/guest-vn-env.txt"
# In the autologon user's session, as the probes run (Run-Probe.ps1):
# Vulkan does not enumerate devices from SYSTEM's session 0. A .cmd wrapper
# sets FBGUEST_VN_PERF (default no_fence_feedback) as VN_PERF for the test
# process only, so that its fence waits ask the renderer and only its own
# buffer reads judge whether the host GPU's writes arrive.
VNP=${FBGUEST_VN_PERF-no_fence_feedback}
status=0
for a in x64 x86; do
    say "fbguest $a (VN_PERF=$VNP)"
    r='C:\win3d\results\fbguest'
    gps "Set-Content -Encoding ascii $r\\fbguest-$a.cmd -Value @('@set VN_PERF=$VNP', '@C:\\win3d\\fbguest\\$a\\fbguest.exe ${FBGUEST_ROUNDS:-200} $r\\fbguest-$a.txt'); & C:\\win3d\\guest\\Run-Probe.ps1 -Path $r\\fbguest-$a.cmd -Raw -NoModules -Name fbguest-$a -OutDir $r -TimeoutSec 240 | Out-Null; Get-Content $r\\fbguest-$a.txt" 300 | tee "$OUT/fbguest-$a.txt"
    grep -q '^RESULT PASS' "$OUT/fbguest-$a.txt" || status=1
done
if [ "$KEEP" != --keep ]; then vmlab destroy; else say "kept $LAB in $LABDIR"; fi
exit $status
