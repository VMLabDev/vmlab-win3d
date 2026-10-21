#!/usr/bin/env bash
# Isolated vmlab supervisor for the portable worker's e2e run (own XDG dirs).
export XDG_CONFIG_HOME=/home/wil/dev/vmlab-win3d-wt/portable/.scratch/e2e-xdg/config XDG_RUNTIME_DIR=/home/wil/dev/vmlab-win3d-wt/portable/.scratch/e2e-xdg/run XDG_STATE_HOME=/home/wil/dev/vmlab-win3d-wt/portable/.scratch/e2e-xdg/state
unset VK_ICD_FILENAMES VK_DRIVER_FILES VK_LOADER_DRIVERS_SELECT RENDER_SERVER_EXEC_PATH
cd /home/wil/dev/vmlab-win3d-wt/portable/.scratch/release/clone
vmlab validate labs/acceptance/vmlab.wcl 2>&1 | tail -2 || true
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1 --lab win3d-portable-e2e
rc=$?
echo "E2E_EXIT=$rc"
vmlab daemon stop 2>&1 | tail -1 || true
exit $rc
