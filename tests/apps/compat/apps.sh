# The applications of the compatibility sweep; sourced by run.sh, which
# provides fetch_pin, gps, in_lab, note, say and run_app.
#
# stage_apps copies each pinned download into the guest and installs it the
# way a user would (the Unigine installers run silently as SYSTEM, the zips
# are unpacked under C:\apps). run_all lists the runs: one per application
# and API. app_tick runs every 5 s during a run, app_collect after it.

GA=C:\\apps\\godot
GODOT=$GA\\Godot_v4.7.2-stable_win64.exe
GODOT_DEMO=particles  # 3d_particles.zip unpacks to particles/

staged() { # staged DEST: C:\apps\DEST is already there (from an earlier --reuse run)
    [ "$(gps "Test-Path C:\\apps\\$1\\.staged" 60 | tail -1)" = True ] && note "$1: already staged"
}
mark() { gps "Set-Content C:\\apps\\$1\\.staged ok" 60 >/dev/null; }

stage_zip() { # stage_zip PIN DEST: unpack a pinned zip into C:\apps\DEST
    local f
    staged "$2" && return 0
    f=$(fetch_pin "$1") || return 1
    in_lab vmlab cp "$f" "$VM:C:/apps/$1.zip" >/dev/null || return 1
    gps "New-Item -ItemType Directory -Force C:\\apps\\$2 | Out-Null; tar.exe -xf C:\\apps\\$1.zip -C C:\\apps\\$2; Remove-Item C:\\apps\\$1.zip; 'unpacked $1'" 600 && mark "$2"
}

stage_inno() { # stage_inno PIN DEST: run a pinned Inno Setup installer silently into C:\apps\DEST
    local f
    staged "$2" && return 0
    f=$(fetch_pin "$1") || return 1
    in_lab vmlab cp "$f" "$VM:C:/apps/$1.exe" >/dev/null || return 1
    gps "\$p = Start-Process C:\\apps\\$1.exe -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/DIR=C:\\apps\\$2' -Wait -PassThru; '$1 setup exit {0}' -f \$p.ExitCode; Remove-Item C:\\apps\\$1.exe; Get-ChildItem -Recurse -Depth 1 C:\\apps\\$2 | ForEach-Object FullName" 1800 && mark "$2"
}

stage_apps() {
    say "stage the applications"
    local rc=0
    {
        stage_inno unigine-heaven-4.0 heaven || rc=1
        stage_inno unigine-valley-1.0 valley || rc=1
        stage_zip gzdoom-4.14.2 gzdoom || rc=1
        stage_zip crispy-doom-7.2.0 crispy || rc=1
        stage_zip freedoom-0.13.0 freedoom || rc=1
        if ! staged godot; then
            stage_zip godot-4.7.2 godot-bin && stage_zip "godot-demo-$GODOT_DEMO" godot-demo || rc=1
            # A Godot project runs from its imported resources (.godot/), which
            # the editor writes on first open; --import does that and quits.
            gps "Move-Item C:\\apps\\godot-bin C:\\apps\\godot; Move-Item C:\\apps\\godot-demo\\$GODOT_DEMO $GA\\; Remove-Item -Recurse C:\\apps\\godot-demo
                 \$p = Start-Process '${GODOT%.exe}_console.exe' -ArgumentList '--headless','--path','$GA\\$GODOT_DEMO','--import' -Wait -PassThru -RedirectStandardOutput $GA\\import.txt; 'godot --import exit {0}' -f \$p.ExitCode; Get-Content $GA\\import.txt -Tail 5" 900 && mark godot
        fi
        # GZDoom reads the renderer from its config at start-up; one config per backend.
        gps "foreach (\$b in 0, 1) { Set-Content -Encoding ASCII C:\\apps\\gzdoom\\compat-\$b.ini \"[GlobalSettings]\`r\`nvid_preferbackend=\$b\`r\`nqueryiwad=false\`r\`nvid_fullscreen=false\`r\`n\" }" 60
        gps 'icacls C:\apps /grant "Users:(OI)(CI)M" /T /Q | Out-Null; Get-ChildItem C:\apps | ForEach-Object Name' 600
        return $rc
    } 2>&1 | tee -a "$EV/stage.txt"
    # A missing application fails its runs; the others still run.
    return 0
}

UH='C:\apps\heaven'
UV='C:\apps\valley'
unigine() { # unigine NAME DIR PROJECT CFG API: the Basic launcher's command line (Custom preset:
    # 1280x720 windowed, medium quality, no MSAA, normal tessellation; Direct3D 9 has none)
    local name=$1 dir=$2 proj=$3 cfg=$4 api=$5 tess=TESSELLATION_NORMAL
    [ "$api" = direct3d9 ] && tess=TESSELLATION_DISABLED
    APP_CWD="$dir\\bin" run_app "$name" "$dir\\bin\\$proj.exe" -project_name "$proj" -data_path ../ -engine_config "../data/$cfg" \
        -system_script "${proj,,}/unigine.cpp" -video_app "$api" -video_multisample 0 -video_fullscreen 0 -video_mode 3 \
        -extern_define ",RELEASE,LANGUAGE_EN,QUALITY_MEDIUM,$tess" -extern_plugin ',GPUMonitor'
}

godot() { # godot NAME ARGS...: the demo project, its FPS printed to its log
    local name=$1; shift
    run_app "$name" "$GODOT" --path "$GA\\$GODOT_DEMO" --resolution 1280x720 --print-fps --log-file "$GA\\$name.log" "$@"
}

gzdoom() { # gzdoom NAME BACKEND: Freedoom Phase 2's attract-mode demos, FPS counter on
    run_app "$1" 'C:\apps\gzdoom\gzdoom.exe' -iwad 'C:\apps\freedoom\freedoom-0.13.0\freedoom2.wad' \
        -config "C:\\apps\\gzdoom\\compat-$2.ini" -nosound -width 1280 -height 720 +vid_fps 1 +logfile "C:\\apps\\gzdoom\\$1.log"
}

run_all() {
    unigine heaven-d3d11 "$UH" Heaven heaven_4.0.cfg direct3d11
    unigine heaven-d3d9 "$UH" Heaven heaven_4.0.cfg direct3d9
    unigine heaven-opengl "$UH" Heaven heaven_4.0.cfg opengl
    unigine valley-d3d11 "$UV" Valley valley_1.0.cfg direct3d11
    unigine valley-d3d9 "$UV" Valley valley_1.0.cfg direct3d9
    unigine valley-opengl "$UV" Valley valley_1.0.cfg opengl
    godot godot-d3d12 --rendering-driver d3d12
    godot godot-vulkan --rendering-driver vulkan
    godot godot-opengl --rendering-method gl_compatibility --rendering-driver opengl3
    gzdoom gzdoom-vulkan 1
    gzdoom gzdoom-opengl 0
    # SDL2's default renderer on Windows is Direct3D 9.
    run_app crispy-doom-d3d9 'C:\apps\crispy\crispy-doom.exe' -iwad 'C:\apps\freedoom\freedoom-0.13.0\freedoom2.wad' -window -nosound
}

app_tick() { :; }

ocr_fps() { # ocr_fps NAME DIR REGEX: average the FPS counter the application draws, read
    # with tesseract from the console frames between 20 s and the close
    python3 - "$WORK/shots" "$1" "$2" "$3" "$SECS" <<'PY'
import glob, os, re, statistics, subprocess, sys
shots, name, d, rx, secs = sys.argv[1:6]
frames = sorted(glob.glob(os.path.join(shots, f'{name}-[0-9]*.png')))[4:(int(secs) + 20) // 5]
vals = []
for f in frames:
    txt = subprocess.run(['sh', '-c', f'magick "{f}" -resize 200% -colorspace Gray png:- | tesseract - - --psm 11 2>/dev/null'],
                         capture_output=True, text=True).stdout
    m = re.search(rx, txt, re.I)
    if m:
        vals.append(float(m.group(1)))
with open(os.path.join(d, 'fps-ocr.txt'), 'w') as o:
    o.write(' '.join(f'{v:g}' for v in vals) + '\n')
if vals:
    with open(os.path.join(d, 'fps.txt'), 'w') as o:
        o.write(f'{statistics.mean(vals):.1f} mean of the on-screen counter in {len(vals)} of {len(frames)} frames (OCR)\n')
PY
}

app_collect() { # app_collect NAME DIR: the application's own log, and its FPS where it reports one
    local name=$1 d=$2
    case $name in
        heaven-*|valley-*) ocr_fps "$name" "$d" 'FPS[:;]?\s*([0-9]+(?:\.[0-9]+)?)' ;;
        godot-*)
            in_lab vmlab cp "$VM:C:/apps/godot/$name.log" "$d/godot.log" >/dev/null 2>&1 || return 0
            # --print-fps: "Project FPS: 60 (16.66 mspf)" once a second; skip the first 10 s.
            awk '/Project FPS:/ { n++; if (n > 10) { s += $3; k++ } } END { if (k) printf "%.1f average over %d s (Godot --print-fps)\n", s / k, k }' \
                "$d/godot.log" >"$d/fps.txt"
            [ -s "$d/fps.txt" ] || rm -f "$d/fps.txt" ;;
        gzdoom-*)
            in_lab vmlab cp "$VM:C:/apps/gzdoom/$name.log" "$d/gzdoom.log" >/dev/null 2>&1
            ocr_fps "$name" "$d" '([0-9]+)\s*fps' ;;
    esac
}
