#!/usr/bin/env bash
# Stretch: run unmodified third-party applications in a lab where the
# package is installed and the autologon desktop is up (tests/e2e/run.sh
# --apps calls this; it also runs on its own against a lab kept with --keep).
#
#   tests/e2e/apps.sh <lab-dir> <vm> <evidence-dir>
#
# The applications come from official downloads pinned in tests/e2e/PINS:
# DirectX SDK (June 2010) prebuilt D3D9 and D3D11 samples (with the D3DX
# runtime from the SDK's own DXSETUP redistributable, installed system-wide
# as a game installer would), and Blender 4.2.3 (OpenGL 4.3). Each runs in
# the user's session through tests/guest/Run-Probe.ps1 -Raw for a fixed time
# and is then closed; the console is captured with `vmlab vm screenshot`
# while it runs. Writes <evidence-dir>/apps/: per app the run summary and
# module list, the last console capture, and apps.json. Whether an app
# rendered is judged from the captures; see docs/acceptance.md.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$REPO/tests/e2e
LABDIR=$(cd "$1" && pwd)
VM=$2
EV=$3/apps
CACHE=$REPO/.scratch/e2e/cache
STAGE=$REPO/.scratch/e2e/apps
mkdir -p "$EV" "$STAGE/shots"
find "$EV" -mindepth 1 -delete

note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
fetch_pin() {
    local name=$1 sha url f
    read -r _ sha url < <(grep "^$name " "$HERE/PINS") || return 1
    f=$CACHE/$(basename "$url")
    mkdir -p "$CACHE"
    if [ ! -f "$f" ]; then curl -sSfLo "$f.part" "$url" && mv "$f.part" "$f" || return 1; fi
    echo "$sha  $f" | sha256sum -c --quiet - >&2 || { note "sha256 mismatch for $name"; return 1; }
    echo "$f"
}

stage() {
    local sdk blender
    sdk=$(fetch_pin dxsdk-jun2010) || return 1
    blender=$(fetch_pin blender-4.2.3) || return 1
    local vc64 vc86
    vc64=$(fetch_pin vcredist2010-x64) || return 1
    vc86=$(fetch_pin vcredist2010-x86) || return 1
    if [ ! -f "$STAGE/dxsdk.zip" ]; then
        note "unpacking the DirectX SDK's samples, media and redistributable"
        rm -rf "$STAGE/dxsdk"
        7z x -y -o"$STAGE/dxsdk" "$sdk" "DXSDK/Redist/*" "DXSDK/Samples/Media/*" \
            "DXSDK/Samples/C++/Direct3D/*" "DXSDK/Samples/C++/Direct3D11/*" >/dev/null || return 1
        (cd "$STAGE/dxsdk" && bsdtar -a -cf "$STAGE/dxsdk.zip" --options zip:compression=store DXSDK) || return 1
        rm -rf "$STAGE/dxsdk"
    fi
    note "copying the DirectX SDK and Blender into the guest"
    in_lab vmlab cp "$STAGE/dxsdk.zip" "$VM:C:/apps/dxsdk.zip" || return 1
    in_lab vmlab cp "$blender" "$VM:C:/apps/blender.zip" || return 1
    in_lab vmlab cp "$vc64" "$VM:C:/apps/vcredist_x64.exe" || return 1
    in_lab vmlab cp "$vc86" "$VM:C:/apps/vcredist_x86.exe" || return 1
    gps 'Set-Location C:\apps; tar.exe -xf dxsdk.zip; tar.exe -xf blender.zip; Remove-Item dxsdk.zip, blender.zip
         Get-ChildItem C:\apps | ForEach-Object Name' 1200 || return 1
    note "installing the DirectX End-User Runtime from the SDK (DXSETUP /silent) and the VC++ 2010 runtime"
    gps '$p = Start-Process C:\apps\DXSDK\Redist\DXSETUP.exe -ArgumentList "/silent" -Wait -PassThru; "DXSETUP exit {0}" -f $p.ExitCode
         foreach ($a in "x64", "x86") { $p = Start-Process "C:\apps\vcredist_$a.exe" -ArgumentList "/q", "/norestart" -Wait -PassThru; "vcredist_$a exit {0}" -f $p.ExitCode }
         foreach ($f in "System32\msvcr100.dll", "SysWOW64\msvcr100.dll", "System32\d3dx10_43.dll") { "{0} {1}" -f $f, (Test-Path "$env:windir\$f") }
         foreach ($f in "System32\d3dx9_43.dll", "System32\d3dx11_43.dll", "System32\D3DCompiler_43.dll", "SysWOW64\d3dx9_43.dll", "SysWOW64\D3DCompiler_43.dll") { "{0} {1}" -f $f, (Test-Path "$env:windir\$f") }' 900 \
        | tee "$EV/dxsetup.txt"
}

capture() { in_lab vmlab vm screenshot "$VM" "$1" >/dev/null 2>&1; }

app() { # app NAME SECONDS EXE ARGS...  (Run-Probe -Raw needs at least one argument)
    local name=$1 secs=$2 exe=$3 n=0 pid rc
    local frames=()
    shift 3
    local arglist=
    [ $# -gt 0 ] && arglist="-ArgumentList $(printf "'%s'," "$@" | sed 's/,$//')"
    note "app $name: $exe $*"
    in_lab vmlab exec --timeout $((secs + 400)) "$VM" -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& C:\\win3d\\guest\\Run-Probe.ps1 -Path '$exe' $arglist -Raw -Name $name -OutDir C:\\win3d\\results\\apps -Seconds $secs -TimeoutSec $secs; exit \$LASTEXITCODE" \
        >"$STAGE/$name.out" 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        sleep 4; n=$((n + 1))
        # Blender's first-run splash covers the viewport; Esc closes it (in
        # the DXUT samples Esc quits, so only here).
        [ "$n" = 8 ] && [[ $name == *Blender* ]] && in_lab vmlab vm sendkeys "$VM" esc >/dev/null 2>&1
        capture "$STAGE/shots/$name-$(printf %03d $n).png" && frames+=("$STAGE/shots/$name-$(printf %03d $n).png")
    done
    wait "$pid"; rc=$?
    note "  Run-Probe exit $rc (124 = still running at $secs s, then closed)"
    # A frame two thirds into the run: past start-up, before it was closed.
    if [ ${#frames[@]} -gt 0 ]; then
        magick "${frames[$(( ${#frames[@]} * 2 / 3 ))]}" -resize 50% -strip -colors 256 "$EV/console-$name.png"
    fi
    in_lab vmlab cp "$VM:C:/win3d/results/apps/$name.run.json" "$EV/$name.run.json" >/dev/null 2>&1
    in_lab vmlab cp "$VM:C:/win3d/results/apps/$name.modules.json" "$STAGE/$name.modules.json" >/dev/null 2>&1
    [ -f "$STAGE/$name.modules.json" ] && python3 - "$STAGE/$name.modules.json" "$EV/$name.modules.txt" <<'EOF'
import json, sys
mods = json.load(open(sys.argv[1], encoding='utf-8-sig'))
mods = mods if isinstance(mods, list) else [mods]
with open(sys.argv[2], 'w') as o:
    o.write('# location        graphics  path  (file version)\n')
    for m in mods:
        o.write(f"{m['location']:<16}  {'G' if m['graphics'] else ' '}  {m['path']}  ({m.get('file_version') or ''})\n")
EOF
    echo "$rc" >"$STAGE/$name.rc"
}

stage || { note "staging failed"; exit 1; }
B=C:\\apps\\DXSDK\\Samples\\C++
# D3D9 samples (DXUT; -windowed is their default, passed because Run-Probe
# -Raw needs an argument).
app d3d9-x64-BasicHLSL 30 "$B\\Direct3D\\Bin\\x64\\BasicHLSL.exe" -windowed
app d3d9-x86-BasicHLSL 30 "$B\\Direct3D\\Bin\\x86\\BasicHLSL.exe" -windowed
app d3d9-x64-ParallaxOcclusionMapping 30 "$B\\Direct3D\\Bin\\x64\\ParallaxOcclusionMapping.exe" -windowed
# D3D11 samples.
app d3d11-x64-BasicHLSL11 30 "$B\\Direct3D11\\Bin\\x64\\BasicHLSL11.exe" -windowed
app d3d11-x64-SubD11 40 "$B\\Direct3D11\\Bin\\x64\\SubD11.exe" -windowed
# OpenGL: Blender with its factory settings (default scene, OpenGL backend).
app opengl-x64-Blender 90 'C:\apps\blender-4.2.3-windows-x64\blender.exe' --factory-startup

python3 - "$EV" "$STAGE" <<'EOF'
import json, os, sys
ev, stage = sys.argv[1:3]
out = {}
for f in sorted(os.listdir(ev)):
    if not f.endswith('.run.json'):
        continue
    name = f[:-9]
    r = json.load(open(os.path.join(ev, f), encoding='utf-8-sig'))
    out[name] = {
        'exe': r.get('exe'), 'session_id': r.get('session_id'), 'exit_code': r.get('exit_code'),
        'timed_out': r.get('timed_out'), 'app_local_modules': r.get('app_local_modules'),
        'graphics_modules': r.get('graphics_modules'),
        'console_png': f'console-{name}.png' if os.path.exists(os.path.join(ev, f'console-{name}.png')) else None,
    }
json.dump(out, open(os.path.join(ev, 'apps.json'), 'w'), indent=2)
for k, v in out.items():
    print(f"{k}: exit {v['exit_code']} timed_out={v['timed_out']} app_local={len(v['app_local_modules'] or [])} "
          f"graphics={len(v['graphics_modules'] or [])}")
EOF
find "$STAGE/shots" -name '*.png' -delete
