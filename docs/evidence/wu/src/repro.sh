#!/usr/bin/env bash
# Lab network and real Windows Update driver scan (docs/installer.md,
# "Windows Update"; docs/known-limits.md, "Evaluation templates need the lab
# NIC").
#
#   docs/evidence/wu/src/repro.sh [--host-gpu NAME] [--keep]
#
# Needs the bundle in out/package/ (scripts/build.sh). Brings up a fresh lab
# win3d-wu-<gpu> from labs/acceptance/vmlab.wcl (the committed lab, with its
# NAT NIC), then:
#    1 activation: slmgr /dlv as the clone boots, slmgr /ato, slmgr /dlv
#    2 baseline: the template's viogpudo drives the device
#    3 turns Windows Update on in this VM only (Test-WindowsUpdate.ps1 -Action
#      Enable: the template's WU policies off, UsoSvc and wuauserv started)
#    4 Windows Update search (Microsoft.Update.Session, ssWindowsUpdate,
#      drivers included) on the template's driver: what it offers the device
#    5 installs the bundle
#    6 search, download and install of every driver update offered
#    7 UsoClient StartScan, StartDownload, StartInstall, five minutes
#    8 reboot
# Reports go to docs/evidence/wu/windows-11-<gpu>/, with checks.txt judging
# them (check.py). Exit 0 when every check passes.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
HOST_GPU=AMD1
KEEP=0
while [ $# -gt 0 ]; do
    case $1 in
        --host-gpu) HOST_GPU=$2; shift 2 ;;
        --keep) KEEP=1; shift ;;
        *) sed -n '2,22p' "$0" >&2; exit 2 ;;
    esac
done
TDIR=$HOME/.local/share/vmlab/templates/x86_64/windows-11
VER=$(ls "$TDIR" | sort -V | tail -1)
G=${HOST_GPU,,}
LAB=win3d-wu-$G
WORK=$REPO/.scratch/wu-repro/$G
EV=$REPO/docs/evidence/wu/windows-11-$G
VM=win
BUNDLE=$REPO/out/package/vmlab-win3d-$(cat "$REPO/installer/VERSION")
mkdir -p "$WORK/lab" "$EV"
find "$EV" -mindepth 1 -delete
exec > >(tee "$EV/run.log") 2>&1

say() { printf '\n[%s] == %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$WORK/lab" && "$@"); }
wu() { # wu TIMEOUT ACTION [args...]: run Test-WindowsUpdate.ps1 in the guest
    local t=$1; shift
    in_lab vmlab exec --timeout "$t" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
        -File 'C:\win3d-wu\Test-WindowsUpdate.ps1' "$@" | tr -d '\r'
}
N=0
next() { N=$((N + 1)); F=$(printf '%s/%02d-%s' "$EV" "$N" "$1"); } # sets F
bound() { python3 - "$1" <<'EOF'
import json, sys
r = json.load(open(sys.argv[1]))
for k in ("bound", "bound_after"):
    if k in r:
        b = r[k]
        print(f"  {r.get('label')}: {b['inf']} ({b['original']}, {b['provider']}, {b['date']}, {b['version']}, rank {b['rank']}, problem {b['problem_code']})")
if "updates" in r:
    print(f"  search result {r['result_code']} in {r['seconds']} s, {len(r['updates'])} updates, "
          f"{sum(u['type'] == 2 for u in r['updates'])} drivers, {sum(bool(u['for_this_device']) for u in r['updates'])} for the display device")
    for u in r["updates"]:
        print(f"    type {u['type']}: {u['title']} [{u['driver_hardware_id'] or ''}]")
if r.get("install"):
    for u in r["install"]["updates"]:
        print(f"    install {u['title']}: result {u['result_code']} {u['hresult']}")
EOF
}
report() { next "$1"; wu 300 -Action Report -Label "$1" >"$F.json"; bound "$F.json"; }
wait_agent() {
    for _ in $(seq 1 180); do
        in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0
        sleep 10
    done
    echo "guest agent did not answer"; return 1
}
slmgr() { in_lab vmlab exec --timeout 300 "$VM" -- cmd.exe /c "cscript.exe //nologo C:\\Windows\\System32\\slmgr.vbs $*" | tr -d '\r'; }

say "lab $LAB on $HOST_GPU (template windows-11@$VER)"
[ -f "$BUNDLE/manifest.json" ] || { echo "no bundle at $BUNDLE" >&2; exit 1; }
sed -e "s/\"win3d-e2e-windows-11\"/\"$LAB\"/" \
    -e "s#\"x86_64/windows-11@[^\"]*\"#\"x86_64/windows-11@$VER\"#" \
    -e "s/host_gpu = \"AMD1\"/host_gpu = \"$HOST_GPU\"/" \
    "$REPO/labs/acceptance/vmlab.wcl" >"$WORK/lab/vmlab.wcl"
cp "$WORK/lab/vmlab.wcl" "$EV/lab-vmlab.wcl"
{ echo "date: $(date -Is)"; echo "commit: $(git -C "$REPO" rev-parse HEAD)"; echo "vmlab: $(vmlab --version)";
  echo "bundle: $(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m["commit"], m["driver_version"])' "$BUNDLE/manifest.json")"; } >"$EV/host.txt"
in_lab vmlab destroy >/dev/null 2>&1 || true
in_lab vmlab up || exit 1
wait_agent || exit 1
in_lab vmlab exec --timeout 60 "$VM" -- cmd.exe /c 'mkdir C:\win3d-wu' >/dev/null 2>&1
in_lab vmlab cp "$HERE/Test-WindowsUpdate.ps1" "$VM:C:/win3d-wu/Test-WindowsUpdate.ps1" || exit 1

say "1 activation"
next activation; { echo "## booted $(date -Is)"; in_lab vmlab exec --timeout 60 "$VM" -- cmd.exe /c ipconfig | tr -d '\r' | grep -E 'IPv4|Gateway';
    echo "## slmgr /dlv"; slmgr /dlv; echo "## slmgr /ato"; slmgr /ato; echo "## slmgr /dlv"; slmgr /dlv; echo "## slmgr /xpr"; slmgr /xpr; } >"$F.txt" 2>&1
grep -E 'License Status|Notification Reason|expire|activated' "$F.txt" | sed 's/^/  /'

say "2 baseline"
report baseline
say "3 Windows Update on"
next enable; wu 300 -Action Enable -Label enable >"$F.json"
say "4 search on the template's driver"
next search-template; wu 3600 -Action Search -Label search-template >"$F.json"; bound "$F.json"
say "5 install the bundle"
next install; "$REPO/scripts/install-into-vm.sh" "$WORK/lab" "$VM" >"$F.log" 2>&1; echo "  install-into-vm.sh -> $?"
report installed
say "6 search, download and install every driver update"
next wu-install; wu 3600 -Action Install -Label wu-install >"$F.json"; bound "$F.json"
report after-wu-install
say "7 UsoClient scan, download, install"
next uso; wu 1200 -Action Uso -Label uso -Seconds 300 >"$F.json"; bound "$F.json"
report after-uso
say "8 reboot"
in_lab vmlab exec --timeout 30 "$VM" -- shutdown.exe /r /t 0 >/dev/null 2>&1 || true
sleep 60; wait_agent; sleep 60
report after-reboot
next activation-final; slmgr /dlv >"$F.txt" 2>&1

say "checks"
python3 "$HERE/check.py" "$EV" | tee "$EV/checks.txt"
rc=${PIPESTATUS[0]}
if [ "$KEEP" = 0 ]; then in_lab vmlab destroy; fi
exit "$rc"
