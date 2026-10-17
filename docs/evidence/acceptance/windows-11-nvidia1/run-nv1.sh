#!/usr/bin/env bash
# One windows-11 boot on the host's DEFAULT GPU (vmlab 0.25: plain
# gpu { mode = "vulkan" }, Nvidia1 = RTX 5090 on helios, stock QEMU).
# Install with the documented command, then capture the qemu.log header,
# vmlab status -v, console screenshots and Run-AllProbes results.
set -uo pipefail
REPO=/home/wil/dev/vmlab-win3d-wt/e2e-final
W=$REPO/.scratch/nv1
LABDIR=$W/lab
EV=$REPO/docs/evidence/acceptance/windows-11-nvidia1
LAB=win3d-e2e-final-nv1
VM=win
mkdir -p "$LABDIR" "$EV"
find "$EV" -mindepth 1 -delete
exec > >(tee "$W/run.log") 2>&1
note() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
in_lab() { (cd "$LABDIR" && "$@"); }
gps() { in_lab vmlab exec --timeout "${2:-600}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
gfile() { local t=$1; shift; in_lab vmlab exec --timeout "$t" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$@" | tr -d '\r'; }
wait_agent() { for _ in $(seq 1 90); do in_lab vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0; sleep 10; done; note "agent did not answer"; return 1; }
shot() { in_lab vmlab vm screenshot "$VM" "$W/$1.png" >"$W/$1.shot.txt" 2>&1 && magick "$W/$1.png" -resize 50% -strip -colors 256 "$EV/console-$1.png"; note "screenshot $1: rc $? $(cat "$W/$1.shot.txt")"; }

TVER=$(ls ~/.local/share/vmlab/templates/x86_64/windows-11 | sort -V | tail -1)
sed -e "s/\"win3d-e2e-windows-11\"/\"$LAB\"/" \
    -e "s#\"x86_64/windows-11@[^\"]*\"#\"x86_64/windows-11@$TVER\"#" \
    -e '/host_gpu *=/d' \
    -e 's#// AMD1 is the host config.*#// No host_gpu: the host default GPU (Nvidia1, RTX 5090, on helios).#' \
    -e '/host.s default GPU on helios is an RTX/d' -e '/with stock QEMU (docs\/known-limits.md)/d' \
    "$REPO/labs/acceptance/vmlab.wcl" >"$LABDIR/vmlab.wcl"
cp "$LABDIR/vmlab.wcl" "$EV/lab-vmlab.wcl"
{ echo "date: $(date -Is)"; echo "commit: $(git -C "$REPO" rev-parse HEAD)"; echo "template: x86_64/windows-11@$TVER"
  echo "vmlab: $(vmlab --version)"; echo "qemu: $(qemu-system-x86_64 --version | head -1)"
  echo "packages: $(pacman -Q qemu-full virglrenderer mesa nvidia-utils 2>/dev/null | tr '\n' ' ')"
  echo "nvidia: $(nvidia-smi --query-gpu=pci.bus_id,name,driver_version --format=csv,noheader 2>/dev/null | tr '\n' ';')"; } | tee "$EV/host.txt"
in_lab vmlab validate 2>&1 | tee "$EV/validate.txt" || exit 1
in_lab vmlab up || { note "vmlab up failed"; }
in_lab vmlab status -v >"$EV/status-v.txt" 2>&1; cat "$EV/status-v.txt"
qlog=${XDG_STATE_HOME:-$HOME/.local/state}/vmlab/labs/$LAB/vms/$VM/qemu.log
grep -E '^vmlab: (argv|env):' "$qlog" >"$EV/qemu-argv-env.txt"
wait_agent || { shot no-agent; in_lab vmlab destroy; exit 1; }
shot before-install
gps 'New-Item -ItemType Directory -Force C:\win3d\results | Out-Null' 60
in_lab vmlab cp "$REPO/tests/guest" "$VM:C:/win3d/guest"
in_lab vmlab cp "$REPO/tests/e2e/guest" "$VM:C:/win3d/e2e"
in_lab vmlab cp "$REPO/out/probes/x64" "$VM:C:/win3d/probes/x64"
in_lab vmlab cp "$REPO/out/probes/x86" "$VM:C:/win3d/probes/x86"
gfile 300 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >"$EV/test-desktop.txt" 2>&1
sleep 60; wait_agent
note "install: scripts/install-into-vm.sh $LABDIR $VM"
"$REPO/scripts/install-into-vm.sh" "$LABDIR" "$VM" 2>&1 | tee "$EV/install.log"
echo "exit: ${PIPESTATUS[0]}" | tee "$EV/install.rc"
wait_agent
gps 'C:\win3d\guest\Get-GpuState.ps1 -OutFile C:\win3d\results\installed-gpu-state.json | Out-Null; Get-Content C:\win3d\results\installed-gpu-state.json -Raw' >"$EV/installed-gpu-state.json"
if ! gps '(Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon").AutoAdminLogon' 60 | grep -q 1; then
    note "autologon gone after install; Set-TestDesktop.ps1 -Reboot again"
    gfile 300 'C:\win3d\guest\Set-TestDesktop.ps1' -Reboot >>"$EV/test-desktop.txt" 2>&1; sleep 60; wait_agent
fi
for _ in $(seq 1 40); do
    r=$(gps '$e = Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0; $d = Get-Process dwm -ErrorAction SilentlyContinue; $up = ((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalSeconds; "{0} dwm={1} {2}" -f [bool]$e, [bool]$d, [int]$up' 60 | tail -1)
    note "desktop: $r"; case $r in "True "*) [ "${r##* }" -ge 120 ] && break ;; esac; sleep 15
done
echo "$r" >"$EV/desktop-state.txt"
in_lab vmlab vm sendkeys "$VM" esc || true; sleep 2
shot desktop
# A guest-side view of the desktop, to tell "black console" from "nothing drawn".
gps 'Add-Type -AssemblyName System.Drawing, System.Windows.Forms; "screen: " + [System.Windows.Forms.Screen]::PrimaryScreen.Bounds' 60 >"$EV/guest-screen.txt" 2>&1
gfile 600 'C:\win3d\e2e\Invoke-Vulkaninfo.ps1' >"$W/vulkaninfo.out" 2>&1
gps 'Get-Content C:\win3d\results\vulkaninfo\vulkaninfo-x64.txt -Raw' >"$EV/vulkaninfo-x64.txt"
grep -E 'deviceName|driverName|exit' "$EV/vulkaninfo-x64.txt"
note "Run-AllProbes"
gfile 1800 'C:\win3d\guest\Run-AllProbes.ps1' -ProbeDir 'C:\win3d\probes' -OutDir 'C:\win3d\results\probes' -Seconds 15 >"$EV/run-allprobes.txt" 2>&1 &
pid=$!; n=0
while kill -0 $pid 2>/dev/null; do sleep 20; n=$((n+1)); [ $n = 2 ] && shot during-probes; done
wait $pid; echo "exit: $?" >>"$EV/run-allprobes.txt"
cat "$EV/run-allprobes.txt"
gps 'Get-Content C:\win3d\results\probes\summary.json -Raw' >"$EV/summary.json"
gps 'Get-WinEvent -FilterHashtable @{LogName="Application"; ProviderName="Application Error","Windows Error Reporting"} -MaxEvents 20 -ErrorAction SilentlyContinue | Format-List TimeCreated, Id, Message | Out-String -Width 200' 120 >"$EV/app-errors.txt" 2>&1
shot after-probes
in_lab vmlab status -v >"$EV/status-v-end.txt" 2>&1
grep -E 'vkr|virgl|error|SIGSEGV' "$qlog" | tail -40 >"$EV/qemu-log-errors.txt"
in_lab vmlab destroy
note done
