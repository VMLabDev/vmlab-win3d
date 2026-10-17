#!/usr/bin/env bash
# Run vmlab (>= 0.24) on Zeus against an isolated supervisor whose PATH puts
# the patched QEMU (patches/host/qemu/0001, built on Zeus by
# patches/host/build-qemu.sh in the staging tree) first, as
# labs/nvidia/vmlab-nv.sh does on helios. Run it from the lab directory, like
# vmlab. The user's supervisor (/run/user/<uid>/vmlab) is never used.
#   labs/zeus/vmlab-nv.sh <vmlab args...>
#   labs/zeus/vmlab-nv.sh --stop-supervisor | --state-home | --which-qemu
# Environment read when the supervisor starts (stop it to change them):
#   WIN3D_QEMU_DIR  directory holding qemu-system-x86_64
#                   (default: <staging>/out/host/qemu/bin)
#   WIN3D_QEMU_ENV  extra NAME=VALUE words for QEMU's environment
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
root=$HOME/win3d-zeus
export XDG_RUNTIME_DIR=$root/run
export XDG_STATE_HOME=$root/state
mkdir -p -m 700 "$XDG_RUNTIME_DIR"; mkdir -p "$XDG_STATE_HOME"
unset VK_ICD_FILENAMES VK_DRIVER_FILES VK_LOADER_DRIVERS_SELECT MESA_VK_DEVICE_SELECT RENDER_SERVER_EXEC_PATH
qdir=${WIN3D_QEMU_DIR:-$repo/out/host/qemu/bin}
export PATH=$qdir:$HOME/.local/bin:$PATH
for kv in ${WIN3D_QEMU_ENV:-}; do export "${kv?}"; done
case ${1:-} in
    --state-home) echo "$XDG_STATE_HOME"; exit 0 ;;
    --which-qemu) echo "$(command -v qemu-system-x86_64) $(qemu-system-x86_64 --version | head -1)"; exit 0 ;;
    --stop-supervisor)
        if pgrep -f "vmlab __labd --lab win3d-zeus-nvidia " >/dev/null; then echo "lab daemon still running; run down first"; exit 1; fi
        vmlab daemon stop; exit $? ;;
esac
[ -x "$qdir/qemu-system-x86_64" ] || { echo "no patched QEMU in $qdir; run patches/host/build-qemu.sh" >&2; exit 1; }
exec vmlab "$@"
