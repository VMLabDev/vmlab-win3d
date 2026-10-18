#!/usr/bin/env bash
# End-to-end acceptance run for the Done items in docs/brief.md.
#
#   tests/e2e/run.sh [--template windows-11|windows-10] [--host-gpu NAME]
#                    [--lab NAME] [--keep] [--apps]
#
# From nothing: builds the package (or reuses out/package/ when it was built
# from a tree identical to HEAD), brings up a fresh lab from
# labs/acceptance/vmlab.wcl (vmlab >= 0.27: gpu { mode = "vulkan" host_gpu
# = NAME }, a GPU declared in the host config ~/.config/vmlab/config.wcl;
# default AMD1, the AMD iGPU; docs/host.md, docs/nvidia.md), records the guest
# baseline, installs with the documented command (scripts/install-into-vm.sh,
# docs/install.md), then in the autologon user's desktop session runs
# vulkaninfo, the D3D9, D3D11, D3D12, OpenGL and Vulkan probes (x64 and
# x86) with `vmlab vm screenshot` console captures of each, and dxdiag. The
# evidence goes to docs/evidence/acceptance/<template>/ (AMD1) or
# <template>-<gpu, lower case>/; tests/e2e/check.py judges it. The lab is
# win3d-e2e-<template>[-<gpu>] unless --lab names it.
# The lab is destroyed unless --keep. --apps also runs the stretch
# applications (tests/e2e/apps.sh) into <evidence>/apps/; they do not
# count towards the exit status.
#
# Exit status: 0 when every Done check passes, 1 otherwise, 2 on usage.
# Lab state lives in .scratch/e2e/<template>[-<gpu>]/lab/.vmlab/.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$REPO/tests/e2e
TEMPLATE=windows-11
HOST_GPU=AMD1
LAB=
KEEP=0
APPS=0
while [ $# -gt 0 ]; do
    case $1 in
        --template) TEMPLATE=${2:-}; shift 2 ;;
        --host-gpu) HOST_GPU=${2:-}; shift 2 ;;
        --lab) LAB=${2:-}; shift 2 ;;
        --keep) KEEP=1; shift ;;
        --apps) APPS=1; shift ;;
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
        *) sed -n '2,26p' "$0" >&2; exit 2 ;;
    esac
done
case $TEMPLATE in windows-11|windows-10) ;; *) echo "unknown template $TEMPLATE" >&2; exit 2 ;; esac
[[ $HOST_GPU =~ ^[A-Za-z0-9_-]+$ ]] || { echo "bad --host-gpu '$HOST_GPU'" >&2; exit 2; }
HOSTCFG=${XDG_CONFIG_HOME:-$HOME/.config}/vmlab/config.wcl
gpu_attr() { # gpu_attr ATTR: a string attribute of the host config's gpu "$HOST_GPU" block
    awk -v g="gpu \"$HOST_GPU\"" -v a="$1" '
        index($0, g) { inb = 1 }
        inb && match($0, "(^|[{ ])" a "[[:space:]]*=[[:space:]]*\"[^\"]*\"") {
            v = substr($0, RSTART, RLENGTH); sub(/^[^"]*"/, "", v); sub(/"$/, "", v); print v; exit }
        inb && /}/ { exit }' "$HOSTCFG"
}
GPU_ADDR=$(gpu_attr address)
[ -n "$GPU_ADDR" ] || { echo "no gpu \"$HOST_GPU\" with an address in $HOSTCFG" >&2; exit 2; }
GPU_QEMU=$(gpu_attr qemu_binary)

TDIR=$HOME/.local/share/vmlab/templates/x86_64/$TEMPLATE
VER=$(ls "$TDIR" 2>/dev/null | sort -V | tail -1)
[ -n "$VER" ] || { echo "no local template $TEMPLATE in $TDIR" >&2; exit 2; }
SUFFIX=; [ "$HOST_GPU" = AMD1 ] || SUFFIX=-${HOST_GPU,,}
LAB=${LAB:-win3d-e2e-$TEMPLATE$SUFFIX}
VM=win
WORK=$REPO/.scratch/e2e/$TEMPLATE$SUFFIX
LABDIR=$WORK/lab
EV=$REPO/docs/evidence/acceptance/$TEMPLATE$SUFFIX
QLOG=${XDG_STATE_HOME:-$HOME/.local/state}/vmlab/labs/$LAB/vms/$VM/qemu.log
SECONDS_PER_PROBE=20

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
        echo "commit: $(git -C "$REPO" rev-parse HEAD)"
        echo "worktree changes: $(git -C "$REPO" status --porcelain | grep -v ' docs/evidence/acceptance/' | wc -l)"
        echo "template: x86_64/$TEMPLATE@$VER"
        echo "kernel: $(uname -r)"
        echo "qemu (PATH): $(qemu-system-x86_64 --version | head -1)"
        [ -z "$GPU_QEMU" ] || echo "qemu ($HOST_GPU qemu_binary): $GPU_QEMU, $("$GPU_QEMU" --version | head -1)"
        echo "packages: $(pacman -Q qemu-full virglrenderer mesa vulkan-radeon nvidia-utils 2>/dev/null | tr '\n' ' ')"
        echo "vmlab: $(vmlab --version)"
        local n node=
        for n in /sys/class/drm/renderD*; do
            [ "$(basename "$(readlink -f "$n/device")")" = "$GPU_ADDR" ] && node=/dev/dri/$(basename "$n")
        done
        echo "venus gpu: host_gpu $HOST_GPU, ${node:-no render node} $GPU_ADDR $(lspci -s "$GPU_ADDR" 2>/dev/null | cut -d' ' -f2-)"
    } | tee "$EV/host.txt"
}

build() {
    say "1 build"
    local bundle reason=
    bundle=$REPO/out/package/vmlab-win3d-$(cat "$REPO/installer/VERSION")
    # Everything that goes into out/package and out/probes.
    local paths=(build installer probes patches scripts/build.sh)
    if [ -f "$bundle/manifest.json" ] && [ -d "$REPO/out/probes/x64" ]; then
        local c
        c=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["commit"])' "$bundle/manifest.json")
        if [[ $c == *-dirty ]]; then reason="bundle built from a dirty tree ($c)"
        elif ! git -C "$REPO" diff --quiet "$c" HEAD -- "${paths[@]}" 2>/dev/null; then reason="sources changed since $c"
        elif [ -n "$(git -C "$REPO" status --porcelain -- "${paths[@]}")" ]; then reason="uncommitted source changes"
        else reason=; MODE="reused (out/package built at $c; no change to ${paths[*]} since)"; fi
    else
        reason="no bundle in out/package"
    fi
    local rc=0
    if [ -n "$reason" ]; then
        MODE="built ($reason)"
        note "building: $reason"
        "$REPO/scripts/build.sh" >"$WORK/build.log" 2>&1 || rc=$?
        tail -5 "$WORK/build.log"
    else
        note "$MODE"
    fi
    {
        echo "mode: $MODE"
        echo "exit: $rc"
        echo "command: scripts/build.sh"
        [ -f "$WORK/build.log" ] && { echo "## stages"; grep '^====' "$WORK/build.log"; }
        echo "## manifest.json"; cat "$bundle/manifest.json" 2>/dev/null
        echo "## SHA256SUMS"; cat "$REPO/out/package/SHA256SUMS" 2>/dev/null
    } >"$EV/build.txt"
    return $rc
}

lab_up() {
    say "2 fresh lab $LAB from x86_64/$TEMPLATE@$VER"
    if [ -f "$LABDIR/vmlab.wcl" ]; then
        note "destroying the previous $LAB"
        in_lab vmlab destroy || true
    fi
    mkdir -p "$LABDIR"
    sed -e "s/\"win3d-e2e-windows-11\"/\"$LAB\"/" \
        -e "s#\"x86_64/windows-11@[^\"]*\"#\"x86_64/$TEMPLATE@$VER\"#" \
        -e "s/host_gpu = \"AMD1\"/host_gpu = \"$HOST_GPU\"/" \
        "$REPO/labs/acceptance/vmlab.wcl" >"$LABDIR/vmlab.wcl"
    cp "$LABDIR/vmlab.wcl" "$EV/lab-vmlab.wcl"
    in_lab vmlab validate || return 1
    in_lab vmlab up || return 1
    grep -q "host_gpu = \"$HOST_GPU\"" "$LABDIR/vmlab.wcl" || { note "lab file does not name host_gpu $HOST_GPU"; return 1; }
    # The host GPU the lab daemon picked (host_gpu=...) and, for a GPU with
    # qemu_binary (vmlab >= 0.26), qemu=<path>.
    in_lab vmlab status -v >"$EV/status-v.txt" 2>&1 || true
    qemu_log_lines
    wait_agent || return 1
    in_lab vmlab osinfo "$VM" >"$EV/osinfo.json" 2>&1 || true
}

qemu_log_lines() {
    # vmlab (>= 0.24) starts qemu.log with the argv and environment it gave
    # QEMU; its PCI-filter layer (>= 0.27) logs the device it kept.
    [ -f "$QLOG" ] && grep -aE '^vmlab: (argv|env):|vmlab-pci-filter:' "$QLOG" >"$EV/qemu-argv-env.txt"
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
    # Set-TestDesktop.ps1 first waits for Windows' user OOBE to finish (vmlab
    # handoff 20261018-092229): OOBE exiting late clears the autologon values.
    gfile 1200 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >"$EV/test-desktop.txt" 2>&1 || true
    grep -E '^oobe:' "$EV/test-desktop.txt" | while read -r l; do note "$l"; done
    sleep 60; wait_agent || return 1
    autologon before-install
}

autologon() { # autologon NAME: record the Winlogon autologon values (not the password)
    gps '$w = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon"
        foreach ($n in "AutoAdminLogon", "DefaultUserName", "DefaultDomainName", "AutoLogonCount", "ForceAutoLogon") { "{0}={1}" -f $n, $w.$n }
        "DefaultPassword set={0}" -f [bool]$w.DefaultPassword
        "console user={0}" -f (Get-CimInstance Win32_ComputerSystem).UserName' 60 >"$EV/autologon-$1.txt" 2>&1
    note "autologon $1: $(tr '\n' ' ' <"$EV/autologon-$1.txt")"
}

session_tests() {
    # The autologon must survive the install's reboots for the session tests.
    # Set-TestDesktop.ps1 waits for OOBE, so this should not fire; if it does,
    # set it up again, and the run log and autologon-fallback.txt say so.
    autologon after-install
    if ! grep -q '^AutoAdminLogon=1' "$EV/autologon-after-install.txt"; then
        note "autologon fallback fired: values gone after install; running Set-TestDesktop.ps1 -Reboot again"
        {
            echo "fired: $(date -Is)"
            gps "Select-String C:\Windows\Panther\UnattendGC\setupact.log -Pattern 'fEnableAutologon|Clearing Auto-logon values' -ErrorAction SilentlyContinue | ForEach-Object Line" 120
        } >"$EV/autologon-fallback.txt" 2>&1
        gfile 1200 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >>"$EV/test-desktop.txt" 2>&1
        sleep 60; wait_agent; autologon after-redo
    fi
    tests
}

install() {
    say "4 install: scripts/install-into-vm.sh $LABDIR $VM"
    local rc=0
    "$REPO/scripts/install-into-vm.sh" "$LABDIR" "$VM" 2>&1 | tee "$EV/install.log"
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
    [ -f "$f" ] || curl -sSfLo "$f.part" "$url" && { [ -f "$f" ] || mv "$f.part" "$f"; } || return 1
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

    say "5b probes"
    for api in d3d9 d3d11 d3d12 opengl vulkan; do
        for arch in x64 x86; do probe "$arch" "$api"; done
    done

    say "5c dxdiag"
    gfile 900 'C:\win3d\guest\Get-Dxdiag.ps1' -OutDir 'C:\win3d\results\dxdiag' -AsUser >"$WORK/raw/dxdiag.out" 2>&1 || true
    tail -5 "$WORK/raw/dxdiag.out"

    if [ "$APPS" = 1 ]; then
        say "5d stretch: third-party applications"
        "$HERE/apps.sh" "$LABDIR" "$VM" "$EV" || note "apps.sh failed"
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
    # The render server logs the PCI-filter line when the guest first opens Vulkan.
    qemu_log_lines
    [ -f "$LABDIR/vmlab.wcl" ] && in_lab vmlab status -v >"$EV/status-v-end.txt" 2>&1
    if [ "$KEEP" = 1 ]; then
        note "--keep: $LAB left running; destroy with (cd $LABDIR && vmlab destroy)"
        return 0
    fi
    [ -f "$LABDIR/vmlab.wcl" ] && in_lab vmlab destroy
    find "$WORK/shots" -name '*.png' -delete
}

say "vmlab-win3d e2e: $TEMPLATE, commit $(git -C "$REPO" rev-parse --short HEAD)"
host_stack
STAGE=ok
if ! build; then STAGE="build failed"
elif ! lab_up; then STAGE="lab bring-up failed"
else
    if ! prepare_guest; then STAGE="guest preparation failed"
    elif ! install; then STAGE="install failed"; session_tests; collect
    else session_tests; collect || STAGE="collect failed"
    fi
fi
teardown
say "result ($STAGE)"
python3 "$HERE/check.py" "$EV"; rc=$?
echo "stage: $STAGE" >>"$EV/results.txt"
[ "$STAGE" = ok ] || rc=1
cp "$WORK/run.log" "$EV/run.log"
exit $rc
