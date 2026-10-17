#!/usr/bin/env bash
# Run vmlab (>= 0.25.0-alpha) against an isolated supervisor, so QEMU can be
# given a patched virglrenderer render server and debug variables. The lab's
# gpu.host_gpu = "Nvidia1" already selects the RTX 5090 (render node and
# Vulkan ICD) through the host config ~/.config/vmlab/config.wcl, which this
# script leaves in place (it changes only XDG_RUNTIME_DIR and XDG_STATE_HOME).
# The user's supervisor is never used.
#   ./vmlab-nv.sh up | status | exec ... | down | destroy ...
#   ./vmlab-nv.sh --stop-supervisor
# Environment read when the supervisor starts (stop it to change them):
#   WIN3D_RENDER_SERVER  absolute path of a virgl_render_server to use instead
#                        of /usr/lib/virgl_render_server
#                        (patches/host/build-virglrenderer.sh builds one)
#   WIN3D_QEMU_DIR       directory holding a qemu-system-x86_64 to use instead
#                        of the host's (patches/host/build-qemu.sh builds one
#                        in out/host/qemu/bin); put first on PATH
#   WIN3D_LAB_DIR        where the lab lives (default .scratch/labs/nvidia-a);
#                        this directory's vmlab.wcl is copied there once
#   WIN3D_QEMU_ENV       extra NAME=VALUE words for QEMU's environment,
#                        e.g. "VKR_LOG_IMAGES=1"
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
export XDG_RUNTIME_DIR=${WIN3D_RUNTIME_DIR:-/tmp/win3d-nvidia-run}
export XDG_STATE_HOME=${WIN3D_STATE_HOME:-$repo/.scratch/nvidia-state}
mkdir -p -m 700 "$XDG_RUNTIME_DIR"; mkdir -p "$XDG_STATE_HOME"
unset VK_ICD_FILENAMES VK_DRIVER_FILES VK_LOADER_DRIVERS_SELECT MESA_VK_DEVICE_SELECT RENDER_SERVER_EXEC_PATH
[ -z "${WIN3D_RENDER_SERVER:-}" ] || export RENDER_SERVER_EXEC_PATH=$WIN3D_RENDER_SERVER
[ -z "${WIN3D_QEMU_DIR:-}" ] || export PATH=$WIN3D_QEMU_DIR:$PATH
for kv in ${WIN3D_QEMU_ENV:-}; do export "${kv?}"; done
labdir=${WIN3D_LAB_DIR:-$repo/.scratch/labs/nvidia-a}
mkdir -p "$labdir"; [ -f "$labdir/vmlab.wcl" ] || cp "$here/vmlab.wcl" "$labdir/vmlab.wcl"
# A copy made before vmlab 0.25 still names the GPU by gpu.address, which 0.25 rejects.
if grep -Eq '^[[:space:]]*(address|vulkan_icd)[[:space:]]*=' "$labdir/vmlab.wcl"; then
  echo "$labdir/vmlab.wcl uses gpu.address (vmlab 0.24); copy $here/vmlab.wcl over it" >&2; exit 1
fi
lab=$(sed -n 's/^lab "\([^"]*\)".*/\1/p' "$labdir/vmlab.wcl")
if [ "${1:-}" = --stop-supervisor ]; then
  # vmlab daemons are not dumpable (their environ is unreadable), so the
  # supervisor is identified as the recorded parent of this lab's daemon.
  if pgrep -f "vmlab __labd --lab $lab " >/dev/null; then echo "lab daemon still running; run ./vmlab-nv.sh down first"; exit 1; fi
  pid=$(cat "$XDG_STATE_HOME/supervisor.pid" 2>/dev/null || true)
  if [ -n "$pid" ] && ps -o cmd= -p "$pid" | grep -q __supervisord; then kill "$pid"; echo "stopped $pid"; else echo "no recorded isolated supervisor"; fi
  exit 0
fi
cd "$labdir"
rc=0; vmlab "$@" || rc=$?
# Record the isolated supervisor (parent of this lab's daemon) for --stop-supervisor.
labd=$(pgrep -f "vmlab __labd --lab $lab " | head -1 || true)
[ -n "$labd" ] && ps -o ppid= -p "$labd" | tr -d ' ' > "$XDG_STATE_HOME/supervisor.pid" || true
exit $rc
