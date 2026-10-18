#!/usr/bin/env bash
# Run vmlab (>= 0.25.0-alpha) on Zeus against an isolated supervisor (state and
# sockets under ~/win3d-zeus/), so QEMU can get a patched QEMU binary, a
# diagnostic render server or debug variables. On vmlab >= 0.26 prefer
# qemu_binary/qemu_env on the host GPU profile instead (docs/nvidia.md).
# Run it from the lab directory, like vmlab. The user's
# supervisor (/run/user/<uid>/vmlab) is never used. The host config
# (~/.config/vmlab/config.wcl, which names the lab's host_gpu) is shared.
#   labs/zeus/vmlab-iso.sh <vmlab args...>
#   labs/zeus/vmlab-iso.sh --stop-supervisor | --state-home | --which-qemu
# Environment read when the supervisor starts (stop it to change them):
#   WIN3D_QEMU_DIR       directory holding the qemu-system-x86_64 to put first
#                        on PATH (default: <staging>/out/host/qemu/bin, the
#                        patches/host/qemu build); "host" for the host's QEMU
#   WIN3D_RENDER_SERVER  a virgl_render_server for RENDER_SERVER_EXEC_PATH
#                        (patches/host/build-virglrenderer.sh builds one)
#   WIN3D_QEMU_ENV       extra NAME=VALUE words for QEMU's environment
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
root=$HOME/win3d-zeus
export XDG_RUNTIME_DIR=$root/run
export XDG_STATE_HOME=$root/state
mkdir -p -m 700 "$XDG_RUNTIME_DIR"; mkdir -p "$XDG_STATE_HOME"
unset VK_ICD_FILENAMES VK_DRIVER_FILES VK_LOADER_DRIVERS_SELECT MESA_VK_DEVICE_SELECT RENDER_SERVER_EXEC_PATH
qdir=${WIN3D_QEMU_DIR:-$repo/out/host/qemu/bin}
if [ "$qdir" != host ]; then
    [ -x "$qdir/qemu-system-x86_64" ] || { echo "no QEMU in $qdir; run patches/host/build-qemu.sh" >&2; exit 1; }
    export PATH=$qdir:$PATH
fi
export PATH=$PATH:$HOME/.local/bin
[ -z "${WIN3D_RENDER_SERVER:-}" ] || export RENDER_SERVER_EXEC_PATH=$WIN3D_RENDER_SERVER
for kv in ${WIN3D_QEMU_ENV:-}; do export "${kv?}"; done
case ${1:-} in
    --state-home) echo "$XDG_STATE_HOME"; exit 0 ;;
    --which-qemu) echo "$(command -v qemu-system-x86_64) $(qemu-system-x86_64 --version | head -1)"; exit 0 ;;
    --stop-supervisor)
        if pgrep -f "vmlab __labd --lab win3d-zeus-" >/dev/null; then echo "a win3d-zeus lab daemon is still running; run down first"; exit 1; fi
        vmlab daemon stop; exit $? ;;
esac
exec vmlab "$@"
