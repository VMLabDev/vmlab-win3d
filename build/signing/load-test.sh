#!/usr/bin/env bash
# Prove a test-signed viogpu3d package installs and loads on a stock vmlab
# windows-11 guest: Secure Boot off, test signing on, the test certificate in
# Root and TrustedPublisher, then pnputil /add-driver /install.
#
#   build/signing/load-test.sh <signed-driver-dir> <evidence-dir> [--keep]
#
# Creates the lab win3d-kmd-build-load under .scratch/labs/ in the repo,
# writes text evidence into <evidence-dir>, and destroys the lab at the end
# unless --keep is given.
#
# The display is virtio-vga-gl (virgl) rendered on the AMD iGPU (prior-art
# 8.1). Its render node is resolved at run time from the GPU's PCI address
# (KMD_LOAD_PCI_ADDRESS, default 0000:72:00.0) through
# /sys/class/drm/renderD*/device, since the kernel numbers render nodes per
# boot; KMD_LOAD_RENDERNODE=/dev/dri/renderDNNN overrides it. The KMD
# refuses to start without the VIRGL feature (viogpu_adapter.cpp, AckFeature
# VIRTIO_GPU_F_VIRGL), so on plain virtio-vga it binds but reports Code 43.
# KMD_LOAD_DISPLAY=virtio-vga selects that plain device instead.
#
#   build/signing/load-test.sh --print-rendernode   only print the resolved node
set -euo pipefail

# The render node of the GPU at a PCI address, from sysfs.
rendernode_of() {
    local d
    for d in /sys/class/drm/renderD*; do
        [ -e "$d/device" ] || continue
        if [ "$(basename "$(readlink -f "$d/device")")" = "$1" ]; then
            echo "/dev/dri/$(basename "$d")"; return 0
        fi
    done
    echo "no render node for PCI address $1 under /sys/class/drm" >&2; return 1
}
PCI_ADDRESS=${KMD_LOAD_PCI_ADDRESS:-0000:72:00.0}
if [ -n "${KMD_LOAD_RENDERNODE:-}" ]; then
    RENDERNODE=$KMD_LOAD_RENDERNODE
else
    RENDERNODE=$(rendernode_of "$PCI_ADDRESS")
fi
if [ "${1:-}" = --print-rendernode ]; then echo "$PCI_ADDRESS -> $RENDERNODE"; exit 0; fi

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
PKG=$(cd "$1" && pwd)
mkdir -p "$2"; EVID=$(cd "$2" && pwd)
KEEP=${3:-}
DISPLAY_DEV=${KMD_LOAD_DISPLAY:-virtio-vga-gl}
LAB=win3d-kmd-build-load
LABDIR=$REPO/.scratch/labs/$LAB
VM=$LAB/win11
CER=$REPO/.signing/test-signing.cer

[ -f "$CER" ] || { echo "missing $CER" >&2; exit 1; }
mkdir -p "$LABDIR"
QEMU_ARGS=
if [ "$DISPLAY_DEV" != virtio-vga ]; then
    QEMU_ARGS="    qemu_args = [\"-display\", \"egl-headless,rendernode=$RENDERNODE\"]"
fi
cat >"$LABDIR/vmlab.wcl" <<WCL
import <vmlab.wcl>

lab "$LAB" {
  vm "win11" {
    template = "x86_64/windows-11"
    cpus = 4
    memory = 8GiB
    secure_boot = false
    prevent_sleep = true
    display = "$DISPLAY_DEV"
$QEMU_ARGS
  }
}
WCL

cleanup() {
    if [ "$KEEP" != --keep ]; then
        (cd "$LABDIR" && vmlab destroy) || true
    fi
}
trap cleanup EXIT

# Evidence commands must not end the run; a failure is itself evidence.
ps() {
    vmlab exec --timeout 600 "$VM" -- powershell.exe -NoProfile -NonInteractive \
        -ExecutionPolicy Bypass -Command "$1" || echo "(exit $?)"
}
gx() { local rc=0; vmlab exec --timeout 900 "$VM" -- "$@" || rc=$?; echo "(exit $rc)"; }

wait_agent() { # the agent drops during a reboot; wait until it answers again
    local i
    for i in $(seq 1 120); do
        vmlab exec --timeout 20 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 && return 0
        sleep 10
    done
    echo "guest did not come back" >&2; return 1
}

reboot_guest() {
    vmlab exec "$VM" -- cmd.exe /c "shutdown /r /t 0 /f" || true
    local i
    for i in $(seq 1 60); do
        vmlab exec --timeout 10 "$VM" -- cmd.exe /c ver >/dev/null 2>&1 || break
        sleep 5
    done
    wait_agent
    sleep 30 # let PnP and services settle
}

DEVQUERY='
foreach ($x in Get-PnpDevice -Class Display -PresentOnly) {
  "InstanceId:   " + $x.InstanceId
  "FriendlyName: " + $x.FriendlyName
  "Status:       " + $x.Status
  foreach ($k in "DEVPKEY_Device_ProblemCode","DEVPKEY_Device_DriverInfPath","DEVPKEY_Device_DriverVersion","DEVPKEY_Device_DriverProvider","DEVPKEY_Device_DriverDate","DEVPKEY_Device_Service") {
    $p = Get-PnpDeviceProperty -InstanceId $x.InstanceId -KeyName $k -ErrorAction SilentlyContinue
    ("{0,-30} {1}" -f ($k + ":"), $p.Data)
  }
  ""
}'
BOUNDINF='(Get-PnpDevice -Class Display -PresentOnly | ForEach-Object { (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_DriverInfPath).Data }) -join ","'
OURINF='(Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -match "\\viogpu3d.inf$" } | Select-Object -First 1).Driver'

echo "== up"
(cd "$LABDIR" && vmlab validate && vmlab up)
wait_agent

{
    echo "# Guest baseline (before any change)"
    echo "## lab file"
    cat "$LABDIR/vmlab.wcl"
    echo "## OS"
    ps '[Environment]::OSVersion.VersionString; $v = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"; $v.DisplayVersion + " build " + $v.CurrentBuild + "." + $v.UBR'
    echo "## Confirm-SecureBootUEFI"
    ps 'try { Confirm-SecureBootUEFI } catch { "error: " + $_.Exception.Message }'
    echo "## bcdedit /enum {current}"
    gx bcdedit.exe /enum "{current}"
    echo "## Display devices"
    ps "$DEVQUERY"
} >"$EVID/01-baseline.txt" 2>&1
cat "$EVID/01-baseline.txt"

echo "== trust the test certificate, enable test signing"
vmlab cp "$PKG" "$VM:C:/win3d/kmd"
vmlab cp "$CER" "$VM:C:/win3d/test-signing.cer"
{
    echo "# Trust and boot policy"
    echo "## certutil -f -addstore Root"
    gx certutil.exe -f -addstore Root 'C:\win3d\test-signing.cer'
    echo "## certutil -f -addstore TrustedPublisher"
    gx certutil.exe -f -addstore TrustedPublisher 'C:\win3d\test-signing.cer'
    echo "## bcdedit /set {current} testsigning on"
    gx bcdedit.exe /set "{current}" testsigning on
    echo "## powercfg /hibernate off (Fast Startup hazard, decision 0001)"
    gx powercfg.exe /hibernate off
} >"$EVID/02-trust.txt" 2>&1
cat "$EVID/02-trust.txt"
reboot_guest

echo "== install"
{
    echo "# Install"
    echo "## bcdedit /enum {current} (after reboot)"
    gx bcdedit.exe /enum "{current}"
    echo "## Package files as copied"
    ps 'Get-ChildItem C:\win3d\kmd | ForEach-Object { "{0,-14} {1,8} {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash }'
    echo "## Get-AuthenticodeSignature (package directory)"
    ps 'Get-ChildItem C:\win3d\kmd -Include *.sys,*.exe,*.cat -Recurse | ForEach-Object { $s = Get-AuthenticodeSignature $_.FullName; "{0,-14} {1,-6} {2}" -f $_.Name, $s.Status, $s.SignerCertificate.Subject }'
    echo "## pnputil /add-driver C:\win3d\kmd\viogpu3d.inf /install"
    gx pnputil.exe /add-driver 'C:\win3d\kmd\viogpu3d.inf' /install
    echo "## Driver ranking for the device (pnputil /enum-devices /class Display /drivers)"
    gx pnputil.exe /enum-devices /class Display /drivers
} >"$EVID/03-install.txt" 2>&1
cat "$EVID/03-install.txt"

BOUND=$(ps "$BOUNDINF" | tr -d '\r')
OURS=$(ps "$OURINF" | tr -d '\r')
echo "bound: $BOUND ours: $OURS"
if [ -n "$OURS" ] && [ "$BOUND" != "$OURS" ]; then
    # A better-ranked package still serves the device. Remove it, as an
    # installer must, and install again.
    {
        echo "# Competing display driver"
        echo "bound after install: $BOUND, this package: $OURS"
        echo "## pnputil /delete-driver $BOUND /uninstall"
        gx pnputil.exe /delete-driver "$BOUND" /uninstall
        echo "## pnputil /add-driver C:\\win3d\\kmd\\viogpu3d.inf /install (again)"
        gx pnputil.exe /add-driver 'C:\win3d\kmd\viogpu3d.inf' /install
    } >"$EVID/03b-remove-competing.txt" 2>&1
    cat "$EVID/03b-remove-competing.txt"
fi

echo "== state right after install"
{ echo "# Display devices right after pnputil"; ps "$DEVQUERY"; } >"$EVID/04-after-install.txt" 2>&1
cat "$EVID/04-after-install.txt"

reboot_guest

echo "== state after reboot"
{
    echo "# After reboot"
    echo "## Display devices"
    ps "$DEVQUERY"
    echo "## pnputil /enum-devices /class Display /drivers"
    gx pnputil.exe /enum-devices /class Display /drivers
    echo "## sc.exe query VioGpu3D"
    gx sc.exe query VioGpu3D
    echo "## sc.exe query vgpusrv"
    gx sc.exe query vgpusrv
    echo "## Kernel service"
    ps 'Get-CimInstance Win32_SystemDriver | Where-Object Name -eq VioGpu3D | Format-List Name,State,Status,PathName,StartMode'
    echo "## Get-AuthenticodeSignature (driver store copies)"
    ps '$dir = Get-ChildItem C:\Windows\System32\DriverStore\FileRepository -Directory -Filter "viogpu3d.inf_*" | Select-Object -First 1; "driver store dir: " + $dir.FullName; Get-ChildItem $dir.FullName -Recurse -Include *.sys,*.cat,*.exe | ForEach-Object { $s = Get-AuthenticodeSignature $_.FullName; "{0,-14} {1,-6} {2} [{3}]" -f $_.Name, $s.Status, $s.SignerCertificate.Subject, $s.StatusMessage }'
    echo "## Code Integrity events naming the package (3004 3033 3089 3077 = rejections)"
    ps '$e = Get-WinEvent -LogName "Microsoft-Windows-CodeIntegrity/Operational" -MaxEvents 1000 -ErrorAction SilentlyContinue | Where-Object { $_.Message -match "viogpu3d|vgpusrv|viogpuap" }; if ($e) { $e | ForEach-Object { "{0} id={1} {2}" -f $_.TimeCreated, $_.Id, ($_.Message -replace "\s+"," ") } } else { "no Code Integrity events mention viogpu3d, vgpusrv or viogpuap" }'
    echo "## Kernel-PnP configuration events for the device (400 configured, 410 started, 411 problem)"
    ps 'Get-WinEvent -LogName "Microsoft-Windows-Kernel-PnP/Configuration" -MaxEvents 500 -ErrorAction SilentlyContinue | Where-Object { $_.Message -match "VEN_1AF4&DEV_1050" } | Select-Object -First 12 | ForEach-Object { "{0} id={1} {2}" -f $_.TimeCreated, $_.Id, ($_.Message -replace "\s+"," ") }'
    echo "## System log: bugchecks and display driver events"
    ps 'Get-WinEvent -LogName System -MaxEvents 500 | Where-Object { $_.Message -match "viogpu|VioGpu3D|vgpusrv|bugcheck" -or $_.ProviderName -match "dxgkrnl|Display" } | Select-Object -First 25 | ForEach-Object { "{0} {1} id={2} {3}" -f $_.TimeCreated, $_.ProviderName, $_.Id, ($_.Message -replace "\s+"," ") }'
} >"$EVID/05-after-reboot.txt" 2>&1
cat "$EVID/05-after-reboot.txt"

echo "== setupapi.dev.log"
ps '$t = Get-Content C:\Windows\INF\setupapi.dev.log; $s = ($t | Select-String -Pattern "/add-driver" -SimpleMatch | Select-Object -First 1).LineNumber; if ($s) { $t[($s-2)..($t.Count-1)] } else { $t }' >"$EVID/06-setupapi-dev-log.txt" 2>&1
grep -n -i -E "sig:|signer|catalog|Installing best|Driver Rank|Exit status" "$EVID/06-setupapi-dev-log.txt" | head -40 || true

echo "== console"
vmlab vm ocr "$VM" >"$EVID/07-console-ocr.txt" 2>&1 || true
echo "evidence written to $EVID"
