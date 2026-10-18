#!/usr/bin/env bash
# Driver-pinning repro: a newer-dated viogpudo must not take the display
# device from this package, and uninstall must undo the pin
# (docs/installer.md, "Evidence").
#
#   docs/evidence/installer/src/repro.sh [--host-gpu NAME] [--keep]
#
# Needs the bundle in out/package/ (scripts/build.sh). Brings up a fresh
# windows-11 lab win3d-installer-<gpu>, pulls the template's viogpudo,
# builds out/installer-repro/ (build.sh here), then:
#    1 baseline
#    2 control: installs the bundle as commit b4b8c3f built it, stages the
#      newer viogpudo with pnputil /add-driver /install (what Windows Update
#      does) and reboots: the device moves to viogpudo
#    3 fix: installs this bundle over it (an upgrade): the device is ours
#      and pinned
#    4 stages the newer and the hostile viogpudo again: no change
#    5 pnputil /scan-devices and UsoClient StartScan: no change
#    6 reboot: no change
#    7 uninstall: the baseline viogpudo is back and the policy is gone
#    8 reinstall with both fakes in the store: pnputil does not bind this
#      package (the hostile one outranks it), the forced install does; a
#      rerun changes nothing
#    9 uninstall again
# Reports go to docs/evidence/installer/windows-11-<gpu>/, with checks.txt
# judging them. Exit 0 when every check passes.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
HOST_GPU=AMD1
KEEP=0
while [ $# -gt 0 ]; do
    case $1 in
        --host-gpu) HOST_GPU=$2; shift 2 ;;
        --keep) KEEP=1; shift ;;
        *) sed -n '2,25p' "$0" >&2; exit 2 ;;
    esac
done
TDIR=$HOME/.local/share/vmlab/templates/x86_64/windows-11
VER=$(ls "$TDIR" | sort -V | tail -1)
G=${HOST_GPU,,}
LAB=win3d-installer-$G
WORK=$REPO/.scratch/installer-repro/$G
EV=$REPO/docs/evidence/installer/windows-11-$G
VM=win
BUNDLE=$REPO/out/package/vmlab-win3d-$(cat "$REPO/installer/VERSION")
mkdir -p "$WORK/lab" "$EV"
find "$EV" -mindepth 1 -delete
exec > >(tee "$EV/run.log") 2>&1

say() { printf '\n[%s] == %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$WORK/lab" && "$@"); }
pin() { # pin ACTION [args...]: run Test-DriverPin.ps1 in the guest
    in_lab vmlab exec --timeout 900 "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
        -File 'C:\win3d-repro\Test-DriverPin.ps1' "$@" | tr -d '\r'
}
N=0
report() { next "$1"; pin -Action Report -Label "$1" >"$F.json"; python3 - "$F.json" <<'EOF'
import json, sys
r = json.load(open(sys.argv[1]))
b = r["bound"]
print(f"  {r['label']}: {b['inf']} ({b['original']}, {b['provider']}, {b['date']}, {b['version']}, rank {b['rank']}, problem {b['problem_code']}); "
      f"policy {r['policy']['values']} {r['policy']['deny_instance_ids']}; installer {r['installer']}")
EOF
}
next() { N=$((N + 1)); F=$(printf '%s/%02d-%s' "$EV" "$N" "$1"); } # sets F
wait_agent() {
    for _ in $(seq 1 90); do
        in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0
        sleep 10
    done
    echo "guest agent did not answer"; return 1
}
reboot() {
    in_lab vmlab exec --timeout 30 "$VM" -- shutdown.exe /r /t 0 >/dev/null 2>&1 || true
    sleep 60
    wait_agent
    sleep 60 # let the startup tasks run
}
install() { # install NAME [--uninstall] [--bundle DIR]
    next "$1"; shift
    "$REPO/scripts/install-into-vm.sh" "$@" "$WORK/lab" "$VM" >"$F.log" 2>&1
    local rc=$?
    echo "  install-into-vm.sh $* -> $rc"
    return $rc
}
journal() { in_lab vmlab exec --timeout 60 "$VM" -- powershell.exe -NoProfile -Command 'Get-Content C:\ProgramData\vmlab-win3d\journal.log' | tr -d '\r' >"$EV/journal-$1.log"; }

say "lab $LAB on $HOST_GPU (template windows-11@$VER)"
[ -f "$BUNDLE/manifest.json" ] || { echo "no bundle at $BUNDLE" >&2; exit 1; }
cat >"$WORK/lab/vmlab.wcl" <<EOF
import <vmlab.wcl>

lab "$LAB" {
  vm "$VM" {
    template = "x86_64/windows-11@$VER"
    secure_boot = false
    gpu {
      mode     = "vulkan"
      host_gpu = "$HOST_GPU"
    }
  }
}
EOF
cp "$WORK/lab/vmlab.wcl" "$EV/lab-vmlab.wcl"
{ echo "date: $(date -Is)"; echo "commit: $(git -C "$REPO" rev-parse HEAD)"; echo "vmlab: $(vmlab --version)";
  echo "bundle: $(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m["commit"], m["driver_version"])' "$BUNDLE/manifest.json")"; } >"$EV/host.txt"
in_lab vmlab destroy >/dev/null 2>&1 || true
in_lab vmlab up || exit 1
wait_agent || exit 1
in_lab vmlab exec --timeout 60 "$VM" -- cmd.exe /c 'mkdir C:\win3d-repro' >/dev/null 2>&1
in_lab vmlab cp "$HERE/Test-DriverPin.ps1" "$VM:C:/win3d-repro/Test-DriverPin.ps1" || exit 1

say "1 baseline"
report baseline

say "pull the template's viogpudo and build the repro inputs"
mapfile -t SRC < <(pin -Action Source)
rm -rf "$WORK/viogpudo"; mkdir -p "$WORK/viogpudo"
for f in "${SRC[@]:1}"; do [[ $f == *.PNF ]] && continue; in_lab vmlab cp "$VM:${SRC[0]}\\$f" "$WORK/viogpudo/$f" || exit 1; done
(cd "$WORK/viogpudo" && sha256sum -- * >"$EV/viogpudo-pulled.sha256")
"$HERE/build.sh" "$WORK/viogpudo" >"$EV/build.log" 2>&1 || { tail "$EV/build.log"; exit 1; }
for d in newer-viogpudo hostile-viogpudo; do in_lab vmlab cp "$REPO/out/installer-repro/$d" "$VM:C:/win3d-repro/$d" || exit 1; done

say "2 control: the installer and INF before the fix"
install install-control --bundle "$REPO/out/installer-repro/control-bundle"
report control-installed
next control-stage-newer; pin -Action Stage -Inf 'C:\win3d-repro\newer-viogpudo\viogpudo.inf' >"$F.json"
reboot
report control-after-newer

say "3 fix: install this bundle (upgrade)"
install install-fixed
report fixed-installed
journal fixed

say "4 stage the newer and the hostile viogpudo again"
next stage-newer; pin -Action Stage -Inf 'C:\win3d-repro\newer-viogpudo\viogpudo.inf' >"$F.json"
next stage-hostile; pin -Action Stage -Inf 'C:\win3d-repro\hostile-viogpudo\viogpudo.inf' >"$F.json"
report after-stage

say "5 pnputil /scan-devices and Windows Update scan"
next scan; pin -Action Scan -Seconds 240 >"$F.json"
report after-scan

say "6 reboot"
reboot
report after-reboot
journal after-reboot

say "7 uninstall"
install uninstall --uninstall
report removed
journal removed

say "8 reinstall with both fakes in the store, then rerun"
install reinstall
report reinstalled
install rerun
report rerun
journal reinstalled

say "9 uninstall again"
install uninstall-again --uninstall
report removed-again
journal removed-again

say "checks"
python3 "$HERE/check.py" "$EV" | tee "$EV/checks.txt"
rc=${PIPESTATUS[0]}
if [ "$KEEP" = 0 ]; then in_lab vmlab destroy; fi
exit "$rc"
