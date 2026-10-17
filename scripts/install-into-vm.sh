#!/usr/bin/env bash
# Install (or uninstall) the vmlab-win3d bundle in a running vmlab Windows VM
# from the host, and wait through the reboots until it finishes.
#
#   scripts/install-into-vm.sh [--uninstall] [--bundle DIR] <lab-dir> <vm>
#
# <lab-dir> is the directory holding the lab's vmlab.wcl; <vm> is the VM
# name as `vmlab exec` takes it there. The default bundle is the one
# scripts/build.sh wrote to out/package/. The bundle is copied to
# C:\vmlab-win3d in the guest with `vmlab cp`, Install.cmd (or Uninstall.cmd)
# runs as SYSTEM through `vmlab exec`, and the script then polls
# C:\ProgramData\vmlab-win3d\state.json until it says Installed (Removed for
# --uninstall) or Failed. Set VMLAB to use another vmlab command.
# Exit status: 0 done, 1 failed (the journal tail is printed), 124 timed out.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
VMLAB=${VMLAB:-vmlab}
TIMEOUT=${INSTALL_TIMEOUT:-2400}
ACTION=Install
WANT=Installed
BUNDLE=
while [ $# -gt 0 ]; do
    case $1 in
        --uninstall) ACTION=Uninstall; WANT=Removed; shift ;;
        --bundle) BUNDLE=$(cd "$2" && pwd); shift 2 ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) break ;;
    esac
done
[ $# -eq 2 ] || { sed -n '2,16p' "$0" >&2; exit 2; }
LABDIR=$(cd "$1" && pwd)
VM=$2
if [ -z "$BUNDLE" ]; then
    BUNDLE=$REPO/out/package/vmlab-win3d-$(cat "$REPO/installer/VERSION")
fi
[ -f "$BUNDLE/manifest.json" ] || { echo "no bundle at $BUNDLE; run scripts/build.sh" >&2; exit 1; }
cd "$LABDIR"

say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
gexec() { "$VMLAB" exec --timeout "${2:-120}" "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1"; }
guest_status() { # prints status|step|last_error, or nothing while the guest is away
    gexec '$p = "C:\ProgramData\vmlab-win3d\state.json"; if (Test-Path $p) { $s = Get-Content $p -Raw | ConvertFrom-Json; "{0}|{1}|{2}" -f $s.status, $s.step, $s.last_error } else { "NotInstalled||" }' 30 2>/dev/null | tr -d '\r' | tail -1
}

say "waiting for the guest agent"
for _ in $(seq 1 60); do guest_status >/dev/null && break; sleep 10; done

if [ "$ACTION" = Install ]; then
    say "copying $(basename "$BUNDLE") to C:\\vmlab-win3d"
    gexec 'if (Test-Path C:\vmlab-win3d) { Remove-Item C:\vmlab-win3d -Recurse -Force }' >/dev/null
    "$VMLAB" cp "$BUNDLE" "$VM:C:/vmlab-win3d"
    CMD='C:\vmlab-win3d\Install.cmd'
else
    CMD='C:\ProgramData\vmlab-win3d\bundle\Uninstall.cmd'
fi

say "running $CMD"
rc=0
"$VMLAB" exec --timeout 1200 "$VM" -- cmd.exe /c "$CMD" || rc=$?
say "$ACTION.cmd exited $rc"

deadline=$((SECONDS + TIMEOUT))
last=
while [ $SECONDS -lt $deadline ]; do
    st=$(guest_status || true)
    if [ -n "$st" ] && [ "$st" != "$last" ]; then say "state: $st"; last=$st; fi
    case ${st%%|*} in
        "$WANT") say "done: $WANT"; exit 0 ;;
        NotInstalled) [ "$ACTION" = Uninstall ] && { say "nothing installed"; exit 0; } ;;
        Failed)
            gexec 'Get-Content C:\ProgramData\vmlab-win3d\journal.log -Tail 40' 60 || true
            exit 1 ;;
    esac
    sleep 15
done
say "timed out after ${TIMEOUT}s (last state: $last)"
exit 124
