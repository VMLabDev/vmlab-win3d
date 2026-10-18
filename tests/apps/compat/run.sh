#!/usr/bin/env bash
# Compatibility sweep: unmodified, freely downloadable Windows applications
# on the installed vmlab-win3d package (docs/compat.md).
#
#   tests/apps/compat/run.sh [--host-gpu NAME] [--bundle DIR] [--reuse]
#                            [--keep] [--only NAME[,NAME...]] [--seconds N]
#                            [--lab-only]
#
# Brings up a fresh Windows 11 lab, win3d-compat-<gpu>, from
# labs/acceptance/vmlab.wcl on host GPU NAME (default Nvidia2), with a NAT
# NIC so the evaluation template does not shut itself down after an hour.
# Sets up the autologon desktop (tests/guest/Set-TestDesktop.ps1), installs
# the bundle with scripts/install-into-vm.sh (default: out/package/), then
# downloads every application in tests/apps/compat/PINS into
# .scratch/compat/cache/ (checking its sha256), installs them in the guest and
# runs each one in the user's desktop session (tests/guest/Run-Probe.ps1
# -Raw) for --seconds (default 150). While it runs the console is captured
# with `vmlab vm screenshot`; at the end it is closed the way a user would,
# with WM_CLOSE to its window (Close-App.ps1), and it must exit by itself
# within 60 s. Per run
# the evidence goes to docs/evidence/compat/<gpu, lower case>/<name>/: three
# console frames (start, middle, end), one 10 s after the close and one after
# the exit, the run summary and module list, the
# application's own log where it writes one, and on a failure the guest's
# error dialogs, its Windows Error Reporting reports (wer.txt), the QEMU/render-server log
# lines of the run and the host kernel's NVIDIA Xid lines. results.json sums it up.
#
# --reuse keeps a running lab of that name with the package installed and
# goes straight to the applications. --only runs the named runs (see
# run_all in apps.sh). --lab-only stops after the install and keeps the lab.
# The lab is destroyed at the end unless --keep or --lab-only.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
HERE=$REPO/tests/apps/compat
HOST_GPU=Nvidia2
BUNDLE=
REUSE=0
KEEP=0
ONLY=
SECS=150
LABONLY=0
while [ $# -gt 0 ]; do
    case $1 in
        --host-gpu) HOST_GPU=${2:-}; shift 2 ;;
        --bundle) BUNDLE=$(cd "$2" && pwd); shift 2 ;;
        --reuse) REUSE=1; shift ;;
        --keep) KEEP=1; shift ;;
        --only) ONLY=${2:-}; shift 2 ;;
        --seconds) SECS=${2:-}; shift 2 ;;
        --lab-only) LABONLY=1; shift ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) sed -n '2,30p' "$0" >&2; exit 2 ;;
    esac
done
[[ $HOST_GPU =~ ^[A-Za-z0-9_-]+$ ]] || { echo "bad --host-gpu '$HOST_GPU'" >&2; exit 2; }
BUNDLE=${BUNDLE:-$REPO/out/package/vmlab-win3d-$(cat "$REPO/installer/VERSION")}
[ -f "$BUNDLE/manifest.json" ] || { echo "no bundle at $BUNDLE; run scripts/build.sh" >&2; exit 2; }
GPU=${HOST_GPU,,}
# Kernel lines that belong to this lab's GPU: NVIDIA Xid lines name the PCI
# address (0000:05:00.0 -> "PCI:0000:05:00"); amdgpu lines name the driver.
GPU_ADDR=$(awk -v g="gpu \"$HOST_GPU\"" 'index($0, g) { inb = 1 } inb && match($0, /address *= *"[^"]*"/) { v = substr($0, RSTART, RLENGTH); sub(/^[^"]*"/, "", v); sub(/"$/, "", v); print v; exit }' "${XDG_CONFIG_HOME:-$HOME/.config}/vmlab/config.wcl")
case $HOST_GPU in Nvidia*) KPAT="Xid \(PCI:${GPU_ADDR%.*}\)" ;; *) KPAT='amdgpu' ;; esac
LAB=win3d-compat-$GPU
VM=win
WORK=$REPO/.scratch/compat/$GPU
LABDIR=$WORK/lab
CACHE=$REPO/.scratch/compat/cache
EV=$REPO/docs/evidence/compat/$GPU
QLOG=${XDG_STATE_HOME:-$HOME/.local/state}/vmlab/labs/$LAB/vms/$VM/qemu.log
mkdir -p "$WORK/shots" "$EV" "$CACHE"
exec > >(tee -a "$WORK/run.log") 2>&1

say() { printf '\n[%s] == %s\n' "$(date +%H:%M:%S)" "$*"; }
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { # gps 'powershell' [timeout]: PowerShell as SYSTEM in the guest
    in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'
}
gfile() { local t=$1; shift; in_lab vmlab exec --timeout "$t" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$@" | tr -d '\r'; }
wait_agent() {
    for _ in $(seq 1 90); do
        in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0
        sleep 10
    done
    note "guest agent did not answer"; return 1
}
wait_desktop() { # the autologon user's explorer.exe, and two minutes of uptime
    local r
    for _ in $(seq 1 60); do
        r=$(gps '$e = Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0; $up = ((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalSeconds; "{0} {1}" -f [bool]$e, [int]$up' 60 2>/dev/null | tail -1)
        case $r in "True "*) [ "${r#True }" -ge 120 ] && return 0 ;; esac
        sleep 15
    done
    return 1
}
fetch_pin() { # fetch_pin NAME: download a PINS entry into the cache, check it, print its path
    local name=$1 sha url f
    read -r _ sha url < <(grep "^$name " "$HERE/PINS") || { note "no pin $name"; return 1; }
    [[ $sha =~ ^[0-9a-f]{64}$ ]] || { note "pin $name has no sha256"; return 1; }
    f=$CACHE/$(basename "$url")
    if [ ! -f "$f" ]; then
        curl -sSfL --retry 5 -o "$f.part.$$" "$url" && mv "$f.part.$$" "$f" || { rm -f "$f.part.$$"; return 1; }
    fi
    echo "$sha  $f" | sha256sum -c --quiet - >&2 || { note "sha256 mismatch for $name"; return 1; }
    echo "$f"
}

host_info() {
    {
        echo "date: $(date -Is)"
        echo "commit: $(git -C "$REPO" rev-parse HEAD)"
        echo "bundle: $BUNDLE ($(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m["version"], m["commit"])' "$BUNDLE/manifest.json"))"
        echo "host_gpu: $HOST_GPU"
        echo "kernel: $(uname -r)"
        echo "packages: $(pacman -Q qemu-full virglrenderer mesa vulkan-radeon nvidia-utils 2>/dev/null | tr '\n' ' ')"
        echo "vmlab: $(vmlab --version)"
    } >"$EV/host.txt"
    cat "$EV/host.txt"
}

lab_up() {
    say "fresh lab $LAB on $HOST_GPU"
    [ -f "$LABDIR/vmlab.wcl" ] && { note "destroying the previous $LAB"; in_lab vmlab destroy || true; }
    mkdir -p "$LABDIR"
    sed -e "s/\"win3d-e2e-windows-11\"/\"$LAB\"/" \
        -e "s/host_gpu = \"AMD1\"/host_gpu = \"$HOST_GPU\"/" \
        -e 's|^\( *\)agent_update = false|&\n\1// Evaluation template: without a NIC it shuts down about an hour after boot.\n\1nic { nat = true }|' \
        "$REPO/labs/acceptance/vmlab.wcl" >"$LABDIR/vmlab.wcl"
    grep -q 'nic { nat = true }' "$LABDIR/vmlab.wcl" || { note "could not add the NIC"; return 1; }
    cp "$LABDIR/vmlab.wcl" "$EV/lab-vmlab.wcl"
    in_lab vmlab up || return 1
    wait_agent || return 1
    local i
    for i in 1 2 3 4 5; do
        gps 'New-Item -ItemType Directory -Force C:\win3d\results, C:\apps | Out-Null' 60 >/dev/null && break
        [ "$i" = 5 ] && return 1
        sleep 20; wait_agent
    done
    in_lab vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest" || return 1
    # The evaluation template shuts itself down an hour after boot unless it
    # is activated, and it does not activate by itself soon enough (wu worker,
    # tests/e2e/run.sh). Recorded, not fatal.
    local slmgr='cscript.exe //nologo C:\Windows\System32\slmgr.vbs'
    in_lab vmlab exec --timeout 300 "$VM" -- cmd.exe /c "$slmgr /ato & $slmgr /xpr" 2>&1 | tr -d '\r' >"$EV/activation.txt" || true
    note "activation: $(tail -1 "$EV/activation.txt")"
    say "test desktop (autologon user win3d)"
    gfile 1200 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >"$WORK/test-desktop.txt" 2>&1 || true
    sleep 60; wait_agent || return 1
    say "install $(basename "$BUNDLE")"
    "$REPO/scripts/install-into-vm.sh" --bundle "$BUNDLE" "$LABDIR" "$VM" >"$EV/install.log" 2>&1
    local rc=$?
    tail -3 "$EV/install.log"
    [ "$rc" = 0 ] || return 1
    wait_agent && wait_desktop || return 1
    # The NAT NIC lets Windows Update pull gigabytes over the host's shared
    # link; the sweep does not need it.
    gps 'foreach ($s in "wuauserv", "bits", "dosvc", "UsoSvc") { Set-Service $s -StartupType Disabled -ErrorAction SilentlyContinue; Stop-Service $s -Force -NoWait -ErrorAction SilentlyContinue }' 120 >/dev/null 2>&1 || true
}

# Every run: NAME PIN-GROUP EXE ARGS... (guest paths). The applications are
# installed by stage_<group>.
. "$HERE/apps.sh"

wer_reports() { # wer_reports DIR: Windows Error Reporting reports written during the run in DIR/run.json
    local t
    t=$(python3 -c 'import json,sys; r=json.load(open(sys.argv[1], encoding="utf-8-sig")); print(r["started"], r["ended"], r["exe"])' "$1/run.json" 2>/dev/null) || return 0
    set -- $t "$1"
    gps "\$s = [datetime]::Parse('$1'); \$e = [datetime]::Parse('$2').AddSeconds(30)
         Get-ChildItem C:\\ProgramData\\Microsoft\\Windows\\WER\\ReportArchive, C:\\ProgramData\\Microsoft\\Windows\\WER\\ReportQueue -Directory -ErrorAction SilentlyContinue |
             Where-Object { \$_.LastWriteTime -ge \$s -and \$_.LastWriteTime -le \$e } | ForEach-Object {
                 \$w = Get-Content (Join-Path \$_.FullName Report.wer) -ErrorAction SilentlyContinue
                 if (\$w -contains 'AppPath=$3') { '-- ' + \$_.Name + ' ' + \$_.LastWriteTime.ToString('o'); \$w | Select-String '^(EventType|Sig\\[|AppPath)' | ForEach-Object Line } }" 120 >"$4/wer.txt" 2>&1
    [ -s "$4/wer.txt" ] || rm -f "$4/wer.txt"
}

collect_failure() { # collect_failure NAME DIR: what a later worker needs to fix a failure
    local name=$1 d=$2 since=$3
    gps '$s = (Get-Date).AddMinutes(-15)
         "== Application log (errors, last 15 min)"
         Get-WinEvent -FilterHashtable @{LogName="Application"; Level=1,2; StartTime=$s} -ErrorAction SilentlyContinue |
             Select-Object -First 20 | ForEach-Object { "{0:o} {1} {2}: {3}" -f $_.TimeCreated, $_.ProviderName, $_.Id, ($_.Message -replace "\s+", " ") }
         "== System log (errors and warnings, last 15 min)"
         Get-WinEvent -FilterHashtable @{LogName="System"; Level=1,2,3; StartTime=$s} -ErrorAction SilentlyContinue |
             Select-Object -First 20 | ForEach-Object { "{0:o} {1} {2}: {3}" -f $_.TimeCreated, $_.ProviderName, $_.Id, ($_.Message -replace "\s+", " ") }
         "== Yttrium UMD log"
         Get-Content C:\ProgramData\Yttrium\yttrium-errors.log -Tail 40 -ErrorAction SilentlyContinue
         "== windows on the user desktop"
         Get-Process | Where-Object { $_.SessionId -gt 0 -and $_.MainWindowTitle } | ForEach-Object { "{0} {1}: {2}" -f $_.Id, $_.ProcessName, $_.MainWindowTitle }' 120 \
        >"$d/guest-errors.txt" 2>&1
    # The QEMU log lines (virglrenderer's render server among them) written during this run.
    [ -f "$QLOG" ] && tail -n +"$((QLINES + 1))" "$QLOG" | grep -av -e '^vmlab: env' -e 'vmlab-pci-filter' | tail -200 >"$d/qemu-log.txt"
}

run_app() { # run_app NAME EXE [ARGS...]; APP_CWD=DIR runs it in DIR (Run-Probe's -OutDir)
    local name=$1 exe=$2 out=${APP_CWD:-C:\\win3d\\results\\compat}; shift 2
    if [ -n "$ONLY" ] && [[ ",$ONLY," != *",$name,"* ]]; then return 0; fi
    local d=$EV/$name n=0 pid rc since t0 closed=
    rm -rf "$d"; mkdir -p "$d"
    find "$WORK/shots" -name "$name-*" -delete
    local arglist=
    [ $# -gt 0 ] && arglist="-ArgumentList $(printf "'%s'," "$@" | sed 's/,$//')"
    say "run $name: $exe $*"
    since=$(date '+%Y-%m-%d %H:%M:%S'); t0=$(date +%s)
    QLINES=$(wc -l <"$QLOG" 2>/dev/null || echo 0)
    # Run-Probe kills the program at -TimeoutSec: the close below comes at
    # 20 s + $SECS, so a program still running 60 s later did not exit cleanly.
    in_lab vmlab exec --timeout $((SECS + 600)) "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& C:\\win3d\\guest\\Run-Probe.ps1 -Path '$exe' $arglist -Raw -Name $name -OutDir '$out' -TimeoutSec $((SECS + 60 + 20)); exit \$LASTEXITCODE" \
        >"$WORK/$name.out" 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        sleep 5; n=$((n + 1))
        in_lab vmlab vm screenshot "$VM" "$WORK/shots/$name-$(printf %03d $n).png" >/dev/null 2>&1
        if [ -z "$closed" ] && [ $(( $(date +%s) - t0 )) -ge $((SECS + 20)) ]; then
            closed=$(date +%s)
            gfile 90 'C:\win3d\compat\Close-App.ps1' -Path "$exe" >"$d/close.txt" 2>&1
            note "  close at $(( closed - t0 )) s: $(tr -d '\n' <"$d/close.txt")"
        fi
        # What the close left on the screen 10 s later (a prompt, a dialog, a hang).
        if [ -n "$closed" ] && [ ! -f "$d/console-closing.png" ] && [ $(( $(date +%s) - closed )) -ge 10 ]; then
            in_lab vmlab vm screenshot "$VM" "$WORK/closing.png" >/dev/null 2>&1 &&
                magick "$WORK/closing.png" -resize 50% -strip -colors 256 "$d/console-closing.png"
        fi
        app_tick "$name" "$(( $(date +%s) - t0 ))"
    done
    wait "$pid"; rc=$?
    note "  Run-Probe exit $rc ($(( $(date +%s) - t0 )) s)"
    in_lab vmlab cp "$VM:${out//\\//}/$name.run.json" "$d/run.json" >/dev/null 2>&1
    in_lab vmlab cp "$VM:${out//\\//}/$name.modules.json" "$WORK/$name.modules.json" >/dev/null 2>&1
    [ -f "$WORK/$name.modules.json" ] && python3 - "$WORK/$name.modules.json" "$d/modules.txt" <<'EOF'
import json, sys
mods = json.load(open(sys.argv[1], encoding='utf-8-sig'))
mods = mods if isinstance(mods, list) else [mods]
with open(sys.argv[2], 'w') as o:
    o.write('# location        graphics  path  (file version)\n')
    for m in mods:
        o.write(f"{m['location']:<16}  {'G' if m['graphics'] else ' '}  {m['path']}  ({m.get('file_version') or ''})\n")
EOF
    # Three frames: about 20 s in, the middle of the run, and just before Alt+F4.
    local shots=("$WORK"/shots/"$name"-*.png) last=$(( (SECS + 20) / 5 ))
    if [ -f "${shots[0]}" ]; then
        local i=0 k
        for k in 4 $(( last / 2 )) $(( last - 1 )); do
            i=$((i + 1))
            [ "$k" -lt "${#shots[@]}" ] || k=$(( ${#shots[@]} - 1 ))
            magick "${shots[$k]}" -resize 50% -strip -colors 256 "$d/console-$i.png"
        done
        # And the console after the close, which shows any error dialog left behind.
        magick "${shots[-1]}" -resize 50% -strip -colors 256 "$d/console-after.png"
    fi
    # The Yttrium UMD's warnings for this process (C:\ProgramData\Yttrium\yttrium-errors.log), counted.
    local gpid
    gpid=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8-sig")).get("pid") or "")' "$d/run.json" 2>/dev/null)
    if [ -n "$gpid" ]; then
        gps "Select-String -LiteralPath C:\\ProgramData\\Yttrium\\yttrium-errors.log -SimpleMatch -Pattern '[warning pid=$gpid ','pid=$gpid ' -ErrorAction SilentlyContinue | ForEach-Object Line" 120 \
            | sed -E 's/^[0-9-]+ [0-9:.]+ //; s/(res_id|zs_res_id|vs|fs|tcs|tes|gs)=[0-9]+/\1=N/g; s/_hash=0x[0-9a-f]+/_hash=X/g' \
            | sort | uniq -c | sort -rn | head -40 >"$d/yttrium-log.txt"
        [ -s "$d/yttrium-log.txt" ] || rm -f "$d/yttrium-log.txt"
    fi
    app_collect "$name" "$d"
    local ok
    ok=$(python3 - "$d/run.json" <<'EOF'
import json, sys
try:
    r = json.load(open(sys.argv[1], encoding='utf-8-sig'))
except Exception:
    print('no-run-json'); sys.exit()
print('clean' if not r.get('timed_out') and r.get('exit_code') == 0 else f"exit={r.get('exit_code')} timed_out={r.get('timed_out')}")
EOF
)
    echo "$ok" >"$d/exit.txt"
    # The host kernel's GPU lines during the run (NVIDIA Xid, amdgpu), whatever the exit.
    journalctl -k --since "$since" --no-pager 2>/dev/null | grep -aE "$KPAT" | tail -40 >"$d/host-kernel.txt"
    [ -s "$d/host-kernel.txt" ] || rm -f "$d/host-kernel.txt"
    if grep -qE 'Xid .*: 79,' "$d/host-kernel.txt" 2>/dev/null || { [[ $HOST_GPU == Nvidia* ]] && ! nvidia-smi >/dev/null 2>&1; }; then
        note "  a GPU fell off the bus or nvidia-smi fails: stopping the sweep (no reset attempted)"
        summarise; exit 3
    fi
    note "  exit: $ok"
    [ "$ok" = clean ] || { collect_failure "$name" "$d" "$since"; wer_reports "$d"; }
    # A program that hung or crashed can leave a dialog or itself behind.
    gps "Get-Process | Where-Object { \$_.SessionId -gt 0 -and \$_.Path -like 'C:\\apps\\*' } | Stop-Process -Force -ErrorAction SilentlyContinue; Get-Process WerFault -ErrorAction SilentlyContinue | Stop-Process -Force" 60 >/dev/null 2>&1
    find "$WORK/shots" -name "$name-*" -delete
}

summarise() {
    python3 - "$EV" <<'EOF'
import json, os, sys
ev = sys.argv[1]
out = {}
for name in sorted(os.listdir(ev)):
    d = os.path.join(ev, name)
    if not os.path.isfile(os.path.join(d, 'exit.txt')):
        continue
    r = {}
    try:
        r = json.load(open(os.path.join(d, 'run.json'), encoding='utf-8-sig'))
    except Exception:
        pass
    e = {'exit': open(os.path.join(d, 'exit.txt')).read().strip(),
         'exit_code': r.get('exit_code'), 'timed_out': r.get('timed_out'), 'duration_s': r.get('duration_s'),
         'session_id': r.get('session_id'), 'app_local_graphics': [m for m in (r.get('graphics_modules') or []) if '[app_local]' in m],
         'graphics_modules': r.get('graphics_modules')}
    f = os.path.join(d, 'fps.txt')
    if os.path.exists(f):
        e['fps'] = open(f).read().strip()
    out[name] = e
json.dump(out, open(os.path.join(ev, 'results.json'), 'w'), indent=2)
for k, v in out.items():
    print(f"{k:28} {v['exit']:24} fps={v.get('fps', '-')}")
EOF
}

say "compat sweep on $HOST_GPU, commit $(git -C "$REPO" rev-parse --short HEAD)"
host_info
if [ "$REUSE" = 1 ] && in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1; then
    note "reusing $LAB"
else
    lab_up || { note "lab bring-up failed"; exit 1; }
fi
[ "$LABONLY" = 1 ] && { note "--lab-only: $LAB is up with the package installed"; exit 0; }
in_lab vmlab cp "$HERE/Close-App.ps1" "$VM:C:/win3d/compat/Close-App.ps1" >/dev/null || exit 1
stage_apps || { note "staging failed"; exit 1; }
run_all
in_lab vmlab status -v >"$WORK/status-v-end.txt" 2>&1
summarise
if [ "$KEEP" = 1 ]; then note "--keep: $LAB left running"; else in_lab vmlab destroy; fi
