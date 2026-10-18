#!/usr/bin/env bash
# The tests/e2e/run.sh acceptance run (branch work/e2e), adapted to run ON
# Zeus, the laptop, against one of its GPUs. docs/zeus.md explains it.
#
#   labs/zeus/run.sh --gpu intel|nvidia|amd [--keep]
#
# --gpu amd is the regression run on helios (AMD iGPU AMD1, 0000:72:00.0), from
# this worktree: WIN3D_WORK=$PWD/.scratch/work WIN3D_E2E=<main's tests/e2e>.
#
# Run it from the staging tree labs/zeus/stage.sh writes to ~/win3d-zeus/repo
# on Zeus: out/package, out/probes, tests/guest, tests/e2e (from work/e2e),
# scripts/install-into-vm.sh, installer/VERSION and labs/zeus. It does not
# build: the package comes from helios (scripts/build.sh at the commit in
# out/package/*/manifest.json). It brings up a fresh lab from
# labs/zeus/<gpu>/vmlab.wcl (vmlab >= 0.25.0-alpha: gpu.host_gpu names a GPU
# the host config declares; labs/zeus/README.md has Zeus's config.wcl),
# records the guest baseline, installs with scripts/install-into-vm.sh, then
# in the autologon user's desktop session runs vulkaninfo, the D3D9, D3D11,
# D3D12, OpenGL and Vulkan probes (x64 and x86) with `vmlab vm screenshot`
# console captures of each, and dxdiag. The evidence goes to
# docs/evidence/zeus/<gpu>/ in the staging tree; tests/e2e/check.py judges it.
# The lab is destroyed unless --keep. WIN3D_GUEST_ENV="NAME=VALUE ..." sets
# machine-wide environment variables in the guest before the install. WIN3D_SMOKE=1 runs only vulkaninfo and
# the x64 D3D11 probe after the install (a short first test of a new GPU).
#
# Both GPUs use the user's supervisor. On vmlab >= 0.26 the host config's
# qemu_binary gives Nvidia1 the patched QEMU (patches/host/build-qemu.sh or
# the release tarball, installed in ~/.local/opt/qemu-vmlab-egl). WIN3D_ISO=1
# runs vmlab through labs/zeus/vmlab-iso.sh instead: an isolated supervisor
# whose WIN3D_* variables pick the QEMU and render server (the 0.24 runs).
#
# For --gpu nvidia a monitor records the kernel's NVRM lines
# (kernel-nvrm.txt) and every change of the GPU's runtime power state
# (runtime-pm.txt) for the whole run. On an Xid 79 (the GPU fell off the
# bus) it captures nvidia-smi and the power state (gpu-lost.txt) and stops
# the run: nothing more touches the GPU. WIN3D_KEEPAWAKE=1 runs
# labs/zeus/keepawake (built on the host if needed) on the GPU for the whole
# run, so runtime power management never suspends it (keepawake.log).
#
# Exit status: 0 when every Done check passes, 1 otherwise, 2 on usage.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
# WIN3D_E2E: where tests/e2e is (the staging tree has it; this branch does not).
HERE=${WIN3D_E2E:-$REPO/tests/e2e}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
export PATH=$HOME/.local/bin:$PATH
GPU=
KEEP=0
while [ $# -gt 0 ]; do
    case $1 in
        --gpu) GPU=${2:-}; shift 2 ;;
        --keep) KEEP=1; shift ;;
        -h|--help) sed -n '2,/^set -uo/p' "$0" | sed '$d'; exit 0 ;;
        *) sed -n '2,/^set -uo/p' "$0" | sed '$d' >&2; exit 2 ;;
    esac
done
# PCI is for the host.txt record only; the lab names the GPU with host_gpu.
case $GPU in
    intel) PCI=0000:00:02.0; HOST_GPU=Intel1 ;;
    nvidia) PCI=0000:01:00.0; HOST_GPU=Nvidia1 ;;
    amd) PCI=0000:72:00.0; HOST_GPU=AMD1 ;;  # helios' AMD iGPU: the regression run
    *) echo "--gpu intel|nvidia|amd" >&2; exit 2 ;;
esac
# WIN3D_ISO=1 goes through the isolated supervisor.
if [ "${WIN3D_ISO:-0}" = 1 ]; then ISO=1; export VMLAB=$REPO/labs/zeus/vmlab-iso.sh
else ISO=0; export VMLAB=$HOME/.local/bin/vmlab; fi
vmlab() { "$VMLAB" "$@"; }
vmlab_state() { # the XDG_STATE_HOME the lab's supervisor uses
    if [ "$ISO" = 1 ]; then "$VMLAB" --state-home; else echo "${XDG_STATE_HOME:-$HOME/.local/state}"; fi
}
LAB=win3d-zeus-$GPU
VM=win
WORK=${WIN3D_WORK:-$HOME/win3d-zeus/work}/$GPU
LABDIR=$WORK/lab
# WIN3D_TAG puts the evidence in docs/evidence/zeus/<gpu>/<tag>/ instead.
EV=$REPO/docs/evidence/zeus/$GPU${WIN3D_TAG:+/$WIN3D_TAG}
# WIN3D_BUNDLE installs another bundle (an experiment) instead of out/package's.
BUNDLE=${WIN3D_BUNDLE:-$REPO/out/package/vmlab-win3d-$(cat "$REPO/installer/VERSION")}
SECONDS_PER_PROBE=20
TEMPLATE=$(sed -n 's/.*template = "\([^"]*\)".*/\1/p' "$REPO/labs/zeus/$GPU/vmlab.wcl")

mkdir -p "$WORK" "$EV"
# Fresh evidence and scratch for every run (the lab state is handled by destroy below).
find "$EV" -mindepth 1 -delete
for d in raw shots results; do [ -d "$WORK/$d" ] && find "$WORK/$d" -delete; mkdir -p "$WORK/$d"; done
exec > >(tee "$WORK/run.log") 2>&1

say() { printf '\n[%s] == %s\n' "$(date +%H:%M:%S)" "$*"; }
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { # gps 'powershell' [timeout]: run PowerShell as SYSTEM in the guest
    in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'
}
gfile() { # gfile timeout script.ps1 args...: run a guest script file as SYSTEM
    local t=$1; shift
    in_lab vmlab exec --timeout "$t" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$@" | tr -d '\r'
}
wait_agent() {
    for _ in $(seq 1 90); do
        in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0
        sleep 10
    done
    note "guest agent did not answer"; return 1
}

host_stack() {
    {
        echo "date: $(date -Is)"
        echo "host: $(hostname) $(uname -r), $(nproc) threads, $(free -g | awk '/^Mem/{print $2}') GiB"
        echo "staging: $(cat "$REPO/STAGED" 2>/dev/null)"
        echo "template: $TEMPLATE"
        echo "qemu: $(command -v qemu-system-x86_64) $(qemu-system-x86_64 --version | head -1)"
        local qb; qb=$(awk -v g="$HOST_GPU" '$1 == "gpu" && $2 == "\""g"\"" { f = 1 } f && /qemu_binary/ { gsub(/[" ]/, "", $3); print $3; exit } f && /^}/ { f = 0 }' "$HOME/.config/vmlab/config.wcl" 2>/dev/null)
        [ -n "$qb" ] && echo "qemu_binary ($HOST_GPU): $qb $("$qb" --version | head -1)"
        [ "$ISO" = 1 ] && echo "isolated supervisor: qemu $("$VMLAB" --which-qemu); render server ${WIN3D_RENDER_SERVER:-host}; extra env ${WIN3D_QEMU_ENV:-none}"
        echo "packages: $(pacman -Q qemu-full qemu-base virglrenderer mesa vulkan-intel vulkan-radeon nvidia-utils 2>/dev/null | tr '\n' ' ')"
        echo "vmlab: $(vmlab --version)"
        local rn; rn=$(basename "$(ls -d /sys/bus/pci/devices/$PCI/drm/renderD* 2>/dev/null | head -1)")
        echo "venus gpu: /dev/dri/$rn $PCI $(lspci -s "$PCI" 2>/dev/null | cut -d' ' -f2-)"
        if [ "$GPU" = nvidia ]; then
            nvidia-smi --query-gpu=name,driver_version --format=csv,noheader
            echo "runtime pm: control $(cat /sys/bus/pci/devices/$PCI/power/control), status $(cat /sys/bus/pci/devices/$PCI/power/runtime_status); $(grep -h 'Runtime D3 status' /proc/driver/nvidia/gpus/$PCI/power | tr -s ' '); $(grep -h DynamicPowerManagement: /proc/driver/nvidia/params)"
            echo "keepawake: ${WIN3D_KEEPAWAKE:-0}"
            echo "guest env: ${WIN3D_GUEST_ENV:-none}"
        fi
    } | tee "$EV/host.txt"
}

build() {
    say "1 package (built on helios, staged by labs/zeus/stage.sh)"
    local bundle=$BUNDLE
    {
        echo "mode: staged from helios"
        echo "bundle: $bundle"
        echo "exit: 0"
        echo "command: scripts/build.sh (on helios)"
        echo "## manifest.json"; cat "$bundle/manifest.json" 2>/dev/null
        echo "## SHA256SUMS"; cat "$(dirname "$bundle")/SHA256SUMS" 2>/dev/null
    } >"$EV/build.txt"
    [ -f "$bundle/manifest.json" ]
}

lab_up() {
    say "2 fresh lab $LAB from $TEMPLATE"
    if [ -f "$LABDIR/vmlab.wcl" ]; then
        note "destroying the previous $LAB"
        in_lab vmlab destroy || true
    fi
    mkdir -p "$LABDIR"
    cp "$REPO/labs/zeus/$GPU/vmlab.wcl" "$LABDIR/vmlab.wcl"
    cp "$LABDIR/vmlab.wcl" "$EV/lab-vmlab.wcl"
    in_lab vmlab validate || return 1
    in_lab vmlab up || return 1
    # vmlab >= 0.24 starts qemu.log with the argv and environment it gave QEMU.
    local qlog; qlog=$(vmlab_state)/vmlab/labs/$LAB/vms/$VM/qemu.log
    [ -f "$qlog" ] && grep -E '^vmlab: (argv|env):' "$qlog" >"$EV/qemu-argv-env.txt"
    wait_agent || return 1
    in_lab vmlab osinfo "$VM" >"$EV/osinfo.json" 2>&1 || true
}

prepare_guest() {
    say "3 guest scripts, probes and the baseline"
    local i
    for i in 1 2 3 4 5; do
        gps 'New-Item -ItemType Directory -Force C:\win3d\results | Out-Null' 60 >/dev/null && break
        [ "$i" = 5 ] && return 1
        note "guest not ready (try $i); waiting for the agent"; sleep 20; wait_agent
    done
    in_lab vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest" || return 1
    in_lab vmlab cp "$HERE/guest" "$VM:C:/win3d/e2e" || return 1
    in_lab vmlab cp "$REPO/out/probes/x64" "$VM:C:/win3d/probes/x64" || return 1
    in_lab vmlab cp "$REPO/out/probes/x86" "$VM:C:/win3d/probes/x86" || return 1
    # LunarG's vulkaninfo, without its loader: a cross-check of the installed loader and ICD.
    local zip; zip=$(fetch_pin lunarg-vulkanrt-components) || return 1
    rm -rf "$WORK/raw/lunarg"; mkdir -p "$WORK/raw/lunarg/x64" "$WORK/raw/lunarg/x86"
    for a in x64 x86; do
        bsdtar -xOf "$zip" "VulkanRT-X64-1.4.363.0-Components/$a/vulkaninfo.exe" >"$WORK/raw/lunarg/$a/vulkaninfo.exe"
        in_lab vmlab cp "$WORK/raw/lunarg/$a/vulkaninfo.exe" "$VM:C:/win3d/lunarg/$a/vulkaninfo.exe" || return 1
    done
    gps 'C:\win3d\guest\Get-GpuState.ps1 -OutFile C:\win3d\results\baseline-gpu-state.json | Out-Null; Get-Content C:\win3d\results\baseline-gpu-state.json -Raw' \
        >"$EV/baseline-gpu-state.json" || return 1
    note "baseline: $(python3 -c 'import json,sys; d=json.load(open(sys.argv[1],encoding="utf-8-sig")); print(", ".join("%s (problem %s)" % (x["name"], x["problem_code"]) for x in d["display_devices"]))' "$EV/baseline-gpu-state.json" 2>&1)"

    say "3b test desktop (autologon user win3d)"
    gfile 300 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >"$EV/test-desktop.txt" 2>&1 || true
    sleep 60; wait_agent || return 1
    autologon before-install
    # WIN3D_GUEST_ENV="NAME=VALUE ...": machine-wide guest environment variables,
    # set before the install, whose reboots make every process see them.
    local kv
    for kv in ${WIN3D_GUEST_ENV:-}; do
        gps "[Environment]::SetEnvironmentVariable('${kv%%=*}', '${kv#*=}', 'Machine'); '${kv%%=*}=' + [Environment]::GetEnvironmentVariable('${kv%%=*}', 'Machine')" 60 | tee -a "$EV/guest-env.txt"
    done
}

autologon() { # autologon NAME: record the Winlogon autologon values (not the password)
    gps '$w = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon"
        foreach ($n in "AutoAdminLogon", "DefaultUserName", "DefaultDomainName", "AutoLogonCount", "ForceAutoLogon") { "{0}={1}" -f $n, $w.$n }
        "DefaultPassword set={0}" -f [bool]$w.DefaultPassword
        "console user={0}" -f (Get-CimInstance Win32_ComputerSystem).UserName' 60 >"$EV/autologon-$1.txt" 2>&1
    note "autologon $1: $(tr '\n' ' ' <"$EV/autologon-$1.txt")"
}

session_tests() {
    # The autologon must survive the install's reboots for the session tests;
    # if it did not, set it up again (and the evidence shows it).
    autologon after-install
    if ! grep -q '^AutoAdminLogon=1' "$EV/autologon-after-install.txt"; then
        note "autologon gone after install; running Set-TestDesktop.ps1 -Reboot again"
        gfile 300 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >>"$EV/test-desktop.txt" 2>&1
        sleep 60; wait_agent; autologon after-redo
    fi
    tests
}

install() {
    say "4 install: scripts/install-into-vm.sh $LABDIR $VM"
    local rc=0
    "$REPO/scripts/install-into-vm.sh" --bundle "$BUNDLE" "$LABDIR" "$VM" 2>&1 | tee "$EV/install.log"
    rc=${PIPESTATUS[0]}
    echo "exit: $rc" >"$EV/install.rc"
    gps 'Get-Content C:\ProgramData\vmlab-win3d\journal.log' >"$EV/install-journal.log" 2>&1 || true
    gps 'C:\win3d\guest\Get-GpuState.ps1 -OutFile C:\win3d\results\installed-gpu-state.json | Out-Null; Get-Content C:\win3d\results\installed-gpu-state.json -Raw' \
        >"$EV/installed-gpu-state.json" || true
    return "$rc"
}

wait_desktop() {
    # The autologon user's explorer.exe in a non-zero session, and some uptime for
    # the shell to settle after the install's final reboot.
    for _ in $(seq 1 60); do
        local r
        r=$(gps '$e = Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0; $up = ((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalSeconds; "{0} {1}" -f [bool]$e, [int]$up' 60 2>/dev/null | tail -1)
        note "desktop: $r"
        case $r in "True "*) [ "${r#True }" -ge 120 ] && return 0 ;; esac
        sleep 15
    done
    return 1
}

fetch_pin() { # fetch_pin NAME: download a tests/e2e/PINS entry into the cache, check it, print its path
    local name=$1 sha url f
    read -r _ sha url < <(grep "^$name " "$HERE/PINS") || return 1
    f=$REPO/.scratch/e2e/cache/$(basename "$url")
    mkdir -p "$(dirname "$f")"
    [ -f "$f" ] || { curl -sSfLo "$f.part.$$" "$url" && mv "$f.part.$$" "$f"; } || return 1
    echo "$sha  $f" | sha256sum -c --quiet - >&2 || { note "sha256 mismatch for $name"; return 1; }
    echo "$f"
}

capture() { # capture NAME: one console frame into shots/, its JSON next to it
    local f=$WORK/shots/$1.png
    in_lab vmlab vm screenshot "$VM" "$f" >/dev/null 2>&1 &&
        python3 "$HERE/findprobe.py" "$f" >"$WORK/shots/$1.json"
}

probe() { # probe ARCH API
    local arch=$1 api=$2 name="$1-$2_probe" n=0 pid rc
    note "probe $name"
    gfile 900 'C:\win3d\guest\Run-Probe.ps1' -Path "C:\\win3d\\probes\\$arch\\${api}_probe.exe" \
        -OutDir 'C:\win3d\results\probes' -Seconds "$SECONDS_PER_PROBE" >"$WORK/raw/$name.out" 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        sleep 2; n=$((n + 1))
        capture "$name-$(printf %03d $n)" || true
    done
    wait "$pid"; rc=$?
    note "  Run-Probe exit $rc"
    python3 - "$WORK/shots" "$name" "$EV" <<'EOF'
import glob, json, os, subprocess, sys
shots, name, ev = sys.argv[1:4]
frames = []
for j in sorted(glob.glob(os.path.join(shots, f'{name}-[0-9]*.json'))):
    try:
        d = json.load(open(j))
    except ValueError:
        continue
    frames.append(d)
seen = [d for d in frames if d.get('probe_window')]
crcs = {d['probe_window']['crc'] for d in seen}
out = {'frames': len(frames), 'frames_with_window': len(seen), 'visible': bool(seen),
       'animating': len(crcs) > 1, 'png': None}
if seen:
    pick = seen[len(seen) // 2]
    png = f'console-{name}.png'
    subprocess.run(['magick', pick['png'], '-resize', '50%', '-strip', '-colors', '256', '-define', 'png:compression-level=9',
                    os.path.join(ev, png)], check=True)
    out['png'] = png
    out['window'] = pick['probe_window']
path = os.path.join(ev, 'shots.json')
allj = json.load(open(path)) if os.path.exists(path) else {}
allj[name] = out
json.dump(allj, open(path, 'w'), indent=2)
print(f"  console: {len(seen)}/{len(frames)} frames show the probe window; animating={out['animating']}")
EOF
}

tests() {
    say "5 interactive-session tests"
    wait_desktop || note "desktop did not settle; trying anyway"
    # Dismiss the Start menu a first logon can leave open over the probe windows.
    in_lab vmlab vm sendkeys "$VM" esc || true
    sleep 2
    capture desktop && magick "$WORK/shots/desktop.png" -resize 50% -strip -colors 256 "$EV/console-desktop.png"

    say "5a vulkaninfo --summary"
    gfile 600 'C:\win3d\e2e\Invoke-Vulkaninfo.ps1' >"$WORK/raw/vulkaninfo.out" 2>&1 || true
    grep -E 'deviceName|deviceType|driverName|exit' "$WORK/raw/vulkaninfo.out" || true

    if [ "${WIN3D_SMOKE:-0}" = 1 ]; then
        say "5b smoke: the x64 D3D11 probe only (WIN3D_SMOKE=1)"
        probe x64 d3d11
        return 0
    fi
    say "5b probes"
    for api in d3d9 d3d11 d3d12 opengl vulkan; do
        for arch in x64 x86; do probe "$arch" "$api"; done
    done

    say "5c dxdiag"
    gfile 900 'C:\win3d\guest\Get-Dxdiag.ps1' -OutDir 'C:\win3d\results\dxdiag' -AsUser >"$WORK/raw/dxdiag.out" 2>&1 || true
    tail -5 "$WORK/raw/dxdiag.out"

    # Extra (not a Done item): D3D11 R32 typed UAV loads (labs/zeus/uavtest).
    local uav=$REPO/out/labs-zeus/uavtest/uavtest.exe
    if [ -f "$uav" ]; then
        say "5d uavtest (D3D11 typed UAV loads)"
        # In the user's session (Run-Probe.ps1), through a .cmd for the redirect.
        in_lab vmlab cp "$uav" "$VM:C:/win3d/uavtest.exe" &&
            gps 'New-Item -ItemType Directory -Force C:\win3d\results\uavtest | Out-Null
                Set-Content -Encoding ascii C:\win3d\uavtest.cmd "C:\win3d\uavtest.exe > C:\win3d\results\uavtest\uavtest.txt 2>&1"' 60 >/dev/null &&
            gfile 300 'C:\win3d\guest\Run-Probe.ps1' -Path 'C:\win3d\uavtest.cmd' -Raw -NoModules -Name uavtest -ArgumentList run \
                -OutDir 'C:\win3d\results\uavtest' >"$WORK/raw/uavtest.out" 2>&1
        gps 'Get-Content C:\win3d\results\uavtest\uavtest.txt' 60 | tee "$EV/uavtest.txt"
    fi
}

collect() {
    say "6 collect evidence"
    # vmlab cp pulls files, not directories.
    gps 'Remove-Item C:\win3d\results.zip -ErrorAction SilentlyContinue; Compress-Archive -Path C:\win3d\results\* -DestinationPath C:\win3d\results.zip' || return 1
    in_lab vmlab cp "$VM:C:/win3d/results.zip" "$WORK/results.zip" || return 1
    local r=$WORK/results
    (cd "$r" && bsdtar -xf "$WORK/results.zip") || return 1
    mkdir -p "$EV/probes" "$EV/vulkaninfo" "$EV/dxdiag"
    cp "$r"/vulkaninfo/*vulkaninfo-*.txt "$EV/vulkaninfo/" 2>/dev/null
    cp "$r"/vulkaninfo/vulkaninfo.run.json "$r"/vulkaninfo/vulkaninfo-crashes.txt "$EV/vulkaninfo/" 2>/dev/null
    cp "$r"/dxdiag/dxdiag.json "$EV/dxdiag/" 2>/dev/null
    for f in "$r"/probes/*_probe.json "$r"/probes/*_probe.run.json; do [ -f "$f" ] && cp "$f" "$EV/probes/"; done
    # The full module lists, one line per module: location, path, version.
    for f in "$r"/probes/*.modules.json; do
        [ -f "$f" ] || continue
        python3 - "$f" "$EV/probes/$(basename "$f" .json).txt" <<'EOF'
import json, sys
mods = json.load(open(sys.argv[1], encoding='utf-8-sig'))
mods = mods if isinstance(mods, list) else [mods]
with open(sys.argv[2], 'w') as o:
    o.write('# location        graphics  path  (file version)\n')
    for m in mods:
        o.write(f"{m['location']:<16}  {'G' if m['graphics'] else ' '}  {m['path']}  ({m.get('file_version') or ''})\n")
EOF
    done
}

teardown() {
    say "7 teardown"
    if [ "$KEEP" = 1 ]; then
        note "--keep: $LAB left running; destroy with (cd $LABDIR && vmlab destroy)"
        return 0
    fi
    # QEMU's and the render server's messages (vkr: lines), minus the argv/env.
    local qlog; qlog=$(vmlab_state)/vmlab/labs/$LAB/vms/$VM/qemu.log
    [ -f "$qlog" ] && grep -v '^vmlab: ' "$qlog" | uniq -c | tail -400 >"$EV/qemu-log.txt"
    [ -f "$LABDIR/vmlab.wcl" ] && in_lab vmlab destroy
    find "$WORK/shots" -name '*.png' -delete
}

gpu_monitor() { # NVIDIA: kernel NVRM lines and runtime power state for the whole run
    local pm=/sys/bus/pci/devices/$PCI/power/runtime_status
    journalctl -k -f -n 0 -o short-iso --no-pager 2>&1 | grep --line-buffered -E 'NVRM|Xid|nvidia' >"$EV/kernel-nvrm.txt" &
    MON_PIDS="$!"
    (
        last=
        while :; do
            cur=$(cat "$pm" 2>&1)
            [ "$cur" = "$last" ] || { echo "$(date -Is) $cur"; last=$cur; }
            if [ -z "${lost:-}" ] && grep -q 'Xid.*: 79,' "$EV/kernel-nvrm.txt" 2>/dev/null; then
                lost=1
                {
                    echo "## $(date -Is) Xid 79 seen; the run stops"
                    echo "## kernel"; journalctl -k -b --no-pager -o short-iso | grep -E 'NVRM|Xid' | tail -100
                    echo "## nvidia-smi"; nvidia-smi 2>&1
                    echo "## power"; grep -H . /sys/bus/pci/devices/$PCI/power/{control,runtime_status} 2>&1
                    cat /proc/driver/nvidia/gpus/$PCI/power 2>&1
                } >"$EV/gpu-lost.txt"
                # The trap runs once the foreground command returns: end the
                # guest commands it may be waiting on (one VM at a time on Zeus).
                kill -TERM "$MAIN_PID" 2>/dev/null
                pkill -TERM -u "$(id -u)" -f "$VMLAB exec" 2>/dev/null
            fi
            sleep 1
        done
    ) >"$EV/runtime-pm.txt" 2>&1 &
    MON_PIDS="$MON_PIDS $!"
}

keepawake_start() {
    local ka=$REPO/labs/zeus/keepawake
    [ -x "$ka/keepawake" ] || "$ka/build.sh" || return 1
    "$ka/keepawake" --pci "$PCI" >"$EV/keepawake.log" 2>&1 &
    KA_PID=$!
    sleep 2
    kill -0 "$KA_PID" 2>/dev/null || { cat "$EV/keepawake.log"; return 1; }
    note "keepawake pid $KA_PID: $(head -1 "$EV/keepawake.log")"
}

stop_helpers() {
    if [ -n "${KA_PID:-}" ]; then # it may hang in glFinish on a lost GPU
        kill -TERM "$KA_PID" 2>/dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$KA_PID" 2>/dev/null || break; sleep 1; done
        kill -KILL "$KA_PID" 2>/dev/null
    fi
    # shellcheck disable=SC2086
    [ -n "${MON_PIDS:-}" ] && kill $MON_PIDS 2>/dev/null
    pkill -f "journalctl -k -f -n 0 -o short-iso" 2>/dev/null
    KA_PID= MON_PIDS=
}

gpu_lost() { # TERM from gpu_monitor: stop at once, keep the evidence, touch nothing
    trap - TERM
    say "GPU fell off the bus (Xid 79): stopping"
    STAGE="gpu fell off the bus"
    KEEP=0 teardown
    stop_helpers
    coredumpctl list --no-pager --since "-2h" 2>&1 | tail -5 >"$EV/coredumps.txt"
    python3 "$HERE/check.py" "$EV"
    echo "stage: $STAGE" >>"$EV/results.txt"
    cp "$WORK/run.log" "$EV/run.log"
    exit 1
}

say "vmlab-win3d on zeus: $GPU, $TEMPLATE"
host_stack
STAGE=ok
MAIN_PID=$$
if [ "$GPU" = nvidia ]; then
    trap gpu_lost TERM
    gpu_monitor
    if [ "${WIN3D_KEEPAWAKE:-0}" = 1 ]; then keepawake_start || STAGE="keepawake failed"; fi
fi
if [ "$STAGE" != ok ]; then :
elif ! build; then STAGE="build failed"
elif ! lab_up; then STAGE="lab bring-up failed"
else
    if ! prepare_guest; then STAGE="guest preparation failed"
    elif ! install; then STAGE="install failed"; session_tests; collect
    else session_tests; collect || STAGE="collect failed"
    fi
fi
teardown
stop_helpers
say "result ($STAGE)"
python3 "$HERE/check.py" "$EV"; rc=$?
echo "stage: $STAGE" >>"$EV/results.txt"
[ "$STAGE" = ok ] || rc=1
cp "$WORK/run.log" "$EV/run.log"
exit $rc
