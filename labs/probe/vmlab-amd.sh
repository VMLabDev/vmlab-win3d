#!/usr/bin/env bash
# DEPRECATED with vmlab >= 0.24.0-alpha: use plain `vmlab` with gpu.address
# (labs/probe/vmlab.wcl). Kept only to reproduce the v0.23 probe evidence.
# Run vmlab against an isolated supervisor whose environment selects the AMD
# Vulkan ICD, so QEMU's virglrenderer Venus contexts use RADV on renderD129
# instead of the RTX 5090s. A running supervisor keeps the environment it was
# spawned with, so this never shares the user's supervisor.
#   ./vmlab-amd.sh up | status | down | destroy ...
# Stop the isolated supervisor afterwards with ./vmlab-amd.sh --stop-supervisor
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
export XDG_RUNTIME_DIR=${WIN3D_RUNTIME_DIR:-/tmp/win3d-vmlab-amd}
export XDG_STATE_HOME=${WIN3D_STATE_HOME:-$here/.scratch/state}
mkdir -p -m 700 "$XDG_RUNTIME_DIR"; mkdir -p "$XDG_STATE_HOME"
unset VK_ICD_FILENAMES VK_LOADER_DRIVERS_SELECT MESA_VK_DEVICE_SELECT
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json
if [ "${1:-}" = --stop-supervisor ]; then
  # vmlab daemons are not dumpable (their environ is unreadable), so the
  # supervisor is identified as the recorded parent of this lab's daemon.
  labd=$(pgrep -f "vmlab __labd --lab win3d-host-probe " || true)
  [ -n "$labd" ] && { echo "lab daemon still running; run ./vmlab-amd.sh down first"; exit 1; }
  pid=$(cat "$XDG_STATE_HOME/supervisor.pid" 2>/dev/null || true)
  if [ -n "$pid" ] && ps -o cmd= -p "$pid" | grep -q __supervisord; then kill "$pid"; echo "stopped $pid"; else echo "no recorded isolated supervisor"; fi
  exit 0
fi
cd "$here"
vmlab "$@"
# Record the isolated supervisor (parent of this lab's daemon) for --stop-supervisor.
labd=$(pgrep -f "vmlab __labd --lab win3d-host-probe " || true)
[ -n "$labd" ] && ps -o ppid= -p "$labd" | tr -d ' ' > "$XDG_STATE_HOME/supervisor.pid" || true
