#!/usr/bin/env bash
# Run Microsoft's D3D12 samples (build/d3d12-samples) in a lab where the
# package is installed and the autologon desktop is up.
#
#   tests/apps/d3d12/run.sh [--seconds N] <lab-dir> <vm> <evidence-dir> [sample...]
#
# Copies out/d3d12-samples/x64/ to C:\apps\d3d12-samples (no D3D12\ Agility
# folder and no other DLL next to the executables), installs the Visual C++
# 2015-2022 x64 runtime the /MD builds import (pinned in PINS; system-wide,
# as an application installer would), and runs each sample in the user's
# session through tests/guest/Run-Probe.ps1 -Raw. After N seconds (default
# 30) the sample is closed the way a user closes it: WM_CLOSE from
# `taskkill /im` (no /f) run in the user's session, so the exit code is the
# sample's own. The console is captured with `vmlab vm screenshot` while it
# runs. Writes to <evidence-dir>: per sample the run summary, the module list
# and a console capture, and samples.json.
# Exit status: 0 when every sample exited 0 by itself after the close,
# loaded no app-local module and left a console capture; 1 otherwise.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
HERE=$REPO/tests/apps/d3d12
SECS=30
if [ "${1:-}" = --seconds ]; then SECS=$2; shift 2; fi
[ $# -ge 3 ] || { sed -n '2,19p' "$0" >&2; exit 2; }
LABDIR=$(cd "$1" && pwd)
VM=$2
mkdir -p "$3"
EV=$(cd "$3" && pwd)
shift 3
SAMPLES=("$@")
[ ${#SAMPLES[@]} -gt 0 ] || SAMPLES=(D3D12HelloTriangle D3D12HelloTexture D3D12Bundles D3D12nBodyGravity D3D12ExecuteIndirect)
BIN=$REPO/out/d3d12-samples/x64
CACHE=$REPO/.scratch/apps-d3d12/cache
STAGE=$REPO/.scratch/apps-d3d12/stage/$(basename "$EV")
mkdir -p "$CACHE" "$STAGE/shots"
[ -d "$BIN" ] || { echo "no $BIN; run build/d3d12-samples/build.sh" >&2; exit 2; }

note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
fetch_pin() {
    local name=$1 sha url f
    read -r _ sha url < <(grep "^$name " "$HERE/PINS") || return 1
    f=$CACHE/$(basename "$url")
    if [ ! -f "$f" ]; then curl -sSfL --retry 5 -o "$f.part" "$url" && mv "$f.part" "$f" || return 1; fi
    echo "$sha  $f" | sha256sum -c --quiet - >&2 || { note "sha256 mismatch for $name"; return 1; }
    echo "$f"
}

stage() {
    local vc
    vc=$(fetch_pin vcredist-x64) || return 1
    note "copying the samples, the guest scripts and the VC++ runtime into the guest"
    gps 'foreach ($d in "C:\apps\d3d12-samples", "C:\win3d\guest") { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
         New-Item -ItemType Directory C:\apps, C:\win3d -Force | Out-Null' >/dev/null || return 1
    in_lab vmlab cp "$BIN" "$VM:C:/apps/d3d12-samples" || return 1
    in_lab vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest" || return 1
    in_lab vmlab cp "$vc" "$VM:C:/apps/vc_redist.x64.exe" || return 1
    note "installing the Visual C++ 2015-2022 x64 runtime"
    gps '$p = Start-Process C:\apps\vc_redist.x64.exe -ArgumentList "/install", "/quiet", "/norestart" -Wait -PassThru
         "vc_redist.x64 exit {0}" -f $p.ExitCode
         foreach ($f in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll") { "System32\{0} {1}" -f $f, (Test-Path "$env:windir\System32\$f") }
         Get-ChildItem -Recurse -File C:\apps\d3d12-samples | ForEach-Object { $_.FullName.Substring(20) }' 900 \
        | tee "$EV/stage.txt"
}

capture() { in_lab vmlab vm screenshot "$VM" "$1" >/dev/null 2>&1; }

# close NAME: send WM_CLOSE to NAME.exe from the user's session.
close() {
    gps '$u = (Get-CimInstance Win32_ComputerSystem).UserName
         $t = "win3d-close-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
         $a = New-ScheduledTaskAction -Execute "$env:windir\System32\taskkill.exe" -Argument "/im '"$1"'.exe"
         $p = New-ScheduledTaskPrincipal -UserId $u -LogonType Interactive -RunLevel Limited
         Register-ScheduledTask -TaskName $t -Action $a -Principal $p | Out-Null
         Start-ScheduledTask -TaskName $t; Start-Sleep 3
         "taskkill result {0}" -f (Get-ScheduledTaskInfo -TaskName $t).LastTaskResult
         Unregister-ScheduledTask -TaskName $t -Confirm:$false' 120
}

sample() { # sample NAME
    local name=$1 n=0 pid rc t0 frames=()
    find "$STAGE/shots" -name "$name-*.png" -delete
    note "sample $name"
    # Run-Probe's own timeout is far past the close, so a sample that ignores
    # WM_CLOSE shows up as 124 (killed) rather than a clean exit.
    in_lab vmlab exec --timeout $((SECS + 600)) "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& C:\\win3d\\guest\\Run-Probe.ps1 -Path 'C:\\apps\\d3d12-samples\\$name\\$name.exe' -Raw -Name $name -OutDir C:\\win3d\\results\\d3d12-samples -Seconds $SECS -TimeoutSec $((SECS + 120)); exit \$LASTEXITCODE" \
        >"$STAGE/$name.out" 2>&1 &
    pid=$!
    t0=$SECONDS
    while kill -0 "$pid" 2>/dev/null && [ $((SECONDS - t0)) -lt $((SECS + 10)) ]; do
        sleep 4; n=$((n + 1))
        capture "$STAGE/shots/$name-$(printf %03d $n).png" && frames+=("$STAGE/shots/$name-$(printf %03d $n).png")
    done
    if kill -0 "$pid" 2>/dev/null; then
        close "$name" | tee "$EV/$name.close.txt"
    fi
    wait "$pid"; rc=$?
    note "  Run-Probe exit $rc"
    # A frame two thirds into the run: past start-up, before the close.
    if [ ${#frames[@]} -gt 0 ]; then
        cp "${frames[$(( ${#frames[@]} * 2 / 3 ))]}" "$EV/console-$name.png"
    fi
    in_lab vmlab cp "$VM:C:/win3d/results/d3d12-samples/$name.run.json" "$EV/$name.run.json" >/dev/null 2>&1
    in_lab vmlab cp "$VM:C:/win3d/results/d3d12-samples/$name.modules.json" "$STAGE/$name.modules.json" >/dev/null 2>&1
    # DXVK's DXGI writes <exe>_dxgi.log to the working directory.
    in_lab vmlab cp "$VM:C:/win3d/results/d3d12-samples/${name}_dxgi.log" "$EV/${name}_dxgi.log" >/dev/null 2>&1
    [ -f "$STAGE/$name.modules.json" ] && python3 - "$STAGE/$name.modules.json" "$EV/$name.modules.txt" <<'EOF'
import json, sys
mods = json.load(open(sys.argv[1], encoding='utf-8-sig'))
mods = mods if isinstance(mods, list) else [mods]
with open(sys.argv[2], 'w') as o:
    o.write('# location        graphics  path  (file version)\n')
    for m in mods:
        o.write(f"{m['location']:<16}  {'G' if m['graphics'] else ' '}  {m['path']}  ({m.get('file_version') or ''})\n")
EOF
}

stage || { note "staging failed"; exit 1; }
since=$(gps '(Get-Date).ToString("o")' 60 | tail -1)
for s in "${SAMPLES[@]}"; do sample "$s"; done
# Application crashes during the runs (the samples, and dwm.exe).
gps '$e = Get-WinEvent -FilterHashtable @{ LogName = "Application"; ProviderName = "Application Error", "Windows Error Reporting"; StartTime = [datetime]"'"$since"'" } -ErrorAction SilentlyContinue
     if (-not $e) { "no Application Error events" }
     $e | Sort-Object TimeCreated | ForEach-Object { "{0:o} {1} {2}" -f $_.TimeCreated, $_.ProviderName, (($_.Message -split "`r?`n" | Where-Object { $_ })[0..4] -join " | ") }' 120 \
    >"$EV/app-errors.txt"

python3 - "$EV" "${SAMPLES[@]}" <<'EOF'
import json, os, sys
ev, names = sys.argv[1], sys.argv[2:]
out, ok = {}, True
for name in names:
    f = os.path.join(ev, f'{name}.run.json')
    r = json.load(open(f, encoding='utf-8-sig')) if os.path.exists(f) else {}
    shot = os.path.exists(os.path.join(ev, f'console-{name}.png'))
    v = out[name] = {
        'exit_code': r.get('exit_code'), 'exit_code_hex': r.get('exit_code_hex'),
        'timed_out': r.get('timed_out'), 'session_id': r.get('session_id'),
        'app_local_modules': r.get('app_local_modules'),
        'graphics_modules': r.get('graphics_modules'),
        'console_png': f'console-{name}.png' if shot else None,
    }
    v['pass'] = (v['exit_code'] == 0 and not v['timed_out'] and not v['app_local_modules'] and shot)
    ok &= v['pass']
    print(f"{name}: exit {v['exit_code_hex']} timed_out={v['timed_out']} "
          f"app_local={len(v['app_local_modules'] or [])} -> {'PASS' if v['pass'] else 'FAIL'}")
json.dump(out, open(os.path.join(ev, 'samples.json'), 'w'), indent=2)
sys.exit(0 if ok else 1)
EOF
