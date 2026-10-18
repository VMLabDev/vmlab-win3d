# Acceptance

This page checks the "Done" criteria in `docs/brief.md` one by one, on fresh
vmlab labs from the local `windows-11` and `windows-10` templates. One script
produces all of the evidence, and anyone can run it again:

```sh
tests/e2e/run.sh --template windows-11     # or windows-10; --keep leaves the lab up
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1   # another host GPU (default AMD1)
```

It exits 0 only when every Done check passes, and it prints a pass/fail
table. The tables below come from each run's `results.json`
(`tests/e2e/report.py`).

## What the script does

From nothing, with no manual step:

1. **Build.** Runs `scripts/build.sh`. If `out/package/` was built from a
   commit whose `build/`, `installer/`, `probes/`, `patches/` and
   `scripts/build.sh` match HEAD, it reuses that bundle instead, and
   `build.txt` records which of the two happened.
2. **Lab.** Copies `labs/acceptance/vmlab.wcl` into
   `.scratch/e2e/<template>/lab/`, sets the template and lab name
   (`win3d-e2e-<template>`), and runs `vmlab up`. `--host-gpu <name>`
   replaces `AMD1` with another GPU from the host config and adds
   `-<name in lower case>` to the scratch and evidence directories and the
   lab name; `--lab` names the lab outright. The lab has the
   `labs/probe/vmlab.wcl` shape for vmlab 0.25: `secure_boot = false` and
   `gpu { mode = "vulkan" host_gpu = "AMD1" }`, the AMD iGPU as the host
   config names it (`docs/host.md`). Without `host_gpu`, the lab would take
   the host's default GPU, which on helios is an RTX 5090. It also
   sets `agent_update = false`, because the templates' agent cannot be
   updated in place and a late agent restart broke one windows-10 run.
   `qemu-argv-env.txt` keeps the QEMU command line and environment that vmlab
   logged, and, on vmlab 0.27, each distinct `vmlab-pci-filter:` line with
   its count. `status-v.txt` and `status-v-end.txt` keep the host GPU vmlab
   picked (`host_gpu=AMD1`) and, for a GPU with `qemu_binary`, `qemu=<path>`.
3. **Baseline.** Copies `tests/guest/`, `tests/e2e/guest/` and `out/probes/`
   into the guest and records `tests/guest/Get-GpuState.ps1`
   (`baseline-gpu-state.json`). It then creates the autologon test user with
   `tests/guest/Set-TestDesktop.ps1 -Reboot`, which first waits for
   Windows' user OOBE to finish (`test-desktop.txt`, `oobe:` line; see
   "Findings").
4. **Install.** Runs the documented command from `docs/install.md`,
   `scripts/install-into-vm.sh <lab-dir> <vm>`, unchanged (`install.log`,
   `install-journal.log`, `installed-gpu-state.json`). The script then checks
   that autologon survived the install's restarts (`autologon-*.txt`). If it
   did not, the script logs `autologon fallback fired`, saves the OOBE lines
   of `setupact.log` (`autologon-fallback.txt`) and runs
   `Set-TestDesktop.ps1 -Reboot` again.
5. **Session tests.** These run as the logged-on user in session 1, through
   `tests/guest/Run-Probe.ps1`. First the script presses Esc to close the
   Start menu, then it runs:
   - `vulkaninfo --summary` from System32 and SysWOW64, the loader's own
     tool installed by the package (`tests/e2e/guest/Invoke-Vulkaninfo.ps1`).
     LunarG's vulkaninfo 1.4.363, pinned in `tests/e2e/PINS` and run without
     its own loader, is an extra cross-check of the installed loader and ICD.
   - The D3D9, D3D11, D3D12, OpenGL and Vulkan probes (`probes/README.md`),
     x64 and x86, for 20 s each. Run-Probe samples each process's loaded
     modules (`probes/<arch>-<api>_probe.modules.txt`). While a probe runs,
     `vmlab vm screenshot` captures the console every 2 s.
     `tests/e2e/findprobe.py` looks in each capture for the probe's three
     marker squares. The capture from the middle of the run is kept as
     `console-<arch>-<api>_probe.png`, at half size.
   - `tests/guest/Get-Dxdiag.ps1 -AsUser` (`dxdiag/dxdiag.json`).
6. **Collect.** Copies the results out of the guest into
   `docs/evidence/acceptance/<template>/`, and `tests/e2e/check.py` judges
   them (`results.json`, `results.txt`). With `--apps`, the stretch
   applications run after this, so one that hangs the guest cannot cost
   the Done evidence.
7. **Teardown.** Records the `qemu.log` lines and `vmlab status -v` again,
   then runs `vmlab destroy`, unless `--keep` was given.

### How each Done item is judged

| Done item (`docs/brief.md`) | Check (`tests/e2e/check.py`) |
| --- | --- |
| 1. One script builds the whole package | `scripts/build.sh` exits 0, or the bundle it built matches HEAD's sources |
| 2. One documented install command | `scripts/install-into-vm.sh` exits 0 and the guest's `state.json` says `Installed` |
| 2. `vulkaninfo` lists the Venus device | The installed x64 `vulkaninfo --summary` names a `Virtio-GPU Venus` device that is not a CPU device |
| 2. D3D9, D3D11, D3D12 and OpenGL applications render on the GPU, with no app-local graphics DLLs, and show in a console screenshot | For each API, x64 and x86: the probe exits 0 and its readback matches; the adapter is not a software renderer and is ours (`Hardsoft VirtIO GPU 3D controller` for D3D, Zink on `Virtio-GPU Venus` for OpenGL); no module loads from the exe's directory; every graphics module comes from System32, SysWOW64, a subdirectory of either, or the driver store; the process loads our stack (`viogpu_d3d10.dll`; `d3d12.dll` with `vmlab-win3d\vkd3dcore.dll` and `libvulkan_virtio.dll`; `viogpu_wgl.dll` with `libvulkan_virtio.dll`); and at least one console capture shows the probe window's three marker squares |
| 2. `dxdiag` reports hardware acceleration | `Get-Dxdiag.ps1`'s `hardware_acceleration` is true, which requires D3D and DirectDraw `Enabled`, a full (not display-only) device, and not the Basic Render Driver. The card or driver must also be `viogpu3d` / Hardsoft |
| 3. `docs/` explains install, uninstall, the stack and pins, signing, known limits and the evidence | Not checked by the script; see "Done item 3" below |

Rows marked "extra" are reported but do not decide the result: the x86
vulkaninfo, exactly one Venus device in the x64 vulkaninfo (added for the
NVIDIA runs, so the AMD tables below predate it), the LunarG cross-check, the Vulkan probes, the device state after
install, and dxdiag's Direct3D status on its own.

## Host stack

Each run records the host in `docs/evidence/acceptance/<template>/host.txt`.
For the AMD runs below (the NVIDIA runs: "NVIDIA RTX 5090"):

| Item | Value |
| --- | --- |
| Host GPU used for Venus | AMD Granite Ridge iGPU (Radeon Graphics), PCI `0000:72:00.0`, `/dev/dri/renderD129`, RADV |
| QEMU | 11.1.1 (`qemu-full 11.1.1-1`) |
| virglrenderer | 1.3.0 (`1.3.0-2.1`) |
| Mesa / RADV | 26.2.2 (`mesa 3:26.2.2-2`, `vulkan-radeon 3:26.2.2-2`) |
| vmlab | 0.25.0-alpha |
| Kernel | 7.2.3-1-cachyos |

vmlab 0.25 picked `host_gpu=AMD1` (`status-v.txt`) and started QEMU with
`-device virtio-vga-gl,venus=true,blob=true,hostmem=4G -display
egl-headless,rendernode=/dev/dri/renderD129` and
`VK_LOADER_DRIVERS_SELECT=*radeon*` in its environment
(`qemu-argv-env.txt`). That is the same QEMU line vmlab 0.24 gave from the
lab's `address`.

## Results

**Both templates pass every Done check in item 2, and item 1.** Both runs
tested commit `6105889`, one package, built once:

- `6105889` is main at `f842ad6` (Mesa patches 0001 to 0017, 0020 and 0021;
  `patches/vulkan-tools/0001`; the DXVK, vkd3d-proton and KMD patches) plus
  this branch's switch of `labs/acceptance/vmlab.wcl` and `tests/e2e/run.sh`
  to vmlab 0.25's `host_gpu`. Nothing under `build/`, `installer/`,
  `probes/`, `patches/` or `scripts/build.sh` differs from main.
- **windows-11** built the package with `scripts/build.sh` from an empty
  `out/` (`build.txt`, mode `built`).
- **windows-10** reused that bundle (`build.txt`, mode `reused`, built at
  `6105889`), so both templates ran the same signed files.

Earlier runs in this round, with the same package sources, are replaced by
the final ones:

- windows-11 on vmlab 0.24 at `ee2fac5`: 12 of 12. Its evidence is in git
  history.
- windows-10, first attempt: stopped at `vmlab validate`. vmlab had been
  upgraded to 0.25.0-alpha a few seconds earlier, and 0.25 rejects a lab's
  `gpu` `address`.
- windows-10 on vmlab 0.25 at `ff9aa1b`, through a `run.sh` rewrite of the
  address to `host_gpu` that `labs/acceptance/vmlab.wcl` has since made
  unnecessary: 12 of 12. Its evidence was not committed.

<!-- e2e:results -->
### windows-11

Run 2026-10-17T21:34:50+10:00 at commit `61058896dca4`, template `x86_64/windows-11@26100.1742.5`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-11/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | built; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 32.92 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 34.2 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.81 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.75 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.61 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.91 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 65.09 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 63.5 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.79 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.74 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-10

Run 2026-10-17T21:54:27+10:00 at commit `61058896dca4`, template `x86_64/windows-10@19045.2006.8`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-10/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 27.45 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 28.03 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.86 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.83 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.5 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.28 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 67.38 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 67.66 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.65 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.83 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-10-nvidia1

Host GPU: Nvidia1, /dev/dri/renderD128 0000:01:00.0 VGA compatible controller: NVIDIA Corporation GB202 [GeForce RTX 5090] (rev a1). QEMU: /home/wil/.local/opt/qemu-11.1.1-vmlab-egl/bin/qemu-system-x86_64, QEMU emulator version 11.1.1 (the Nvidia1 qemu_binary).

Run 2026-10-18T11:45:50+10:00 at commit `a58acd326451`, template `x86_64/windows-10@19045.2006.8`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-10-nvidia1/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 18.98 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 22.94 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.52 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.38 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 54.23 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 54.79 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 65.44 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 65.49 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 57.3 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 56.69 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-11-nvidia1

Host GPU: Nvidia1, /dev/dri/renderD128 0000:01:00.0 VGA compatible controller: NVIDIA Corporation GB202 [GeForce RTX 5090] (rev a1). QEMU: /home/wil/.local/opt/qemu-11.1.1-vmlab-egl/bin/qemu-system-x86_64, QEMU emulator version 11.1.1 (the Nvidia1 qemu_binary).

Run 2026-10-18T11:02:15+10:00 at commit `045fda111e0a`, template `x86_64/windows-11@26100.1742.5`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-11-nvidia1/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 22.32 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 21.79 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.68 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.6 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 53.99 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 54.26 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 61.46 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 64.48 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 58.14 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 58.16 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-11-nvidia2

Host GPU: Nvidia2, /dev/dri/renderD130 0000:05:00.0 VGA compatible controller: NVIDIA Corporation GB202 [GeForce RTX 5090] (rev a1). QEMU: /home/wil/.local/opt/qemu-11.1.1-vmlab-egl/bin/qemu-system-x86_64, QEMU emulator version 11.1.1 (the Nvidia2 qemu_binary).

Run 2026-10-18T15:51:38+10:00 at commit `99ad0c1faee6`, template `x86_64/windows-11@26100.1742.5`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-11-nvidia2/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 21.0 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 19.94 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.12 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.03 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 51.39 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 53.24 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 60.63 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 59.88 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 57.72 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 57.76 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |
<!-- /e2e:results -->

## Stretch: unmodified third-party applications

`tests/e2e/run.sh --apps` (that is, `tests/e2e/apps.sh`) also runs real
applications, unmodified, from official downloads pinned by URL and sha256 in
`tests/e2e/PINS`. Each runs in the user's session through `Run-Probe.ps1`
(`-Raw`) for 30 to 90 s and is then closed. `vmlab vm screenshot` captures
the console every 4 s, and the capture from two thirds of the way through is
kept. These results do not count towards the exit status. Evidence:
`docs/evidence/acceptance/<template>/apps/`.

The prerequisites are installed system-wide, as a game installer would do
it: the DirectX End-User Runtime from the SDK's own `Redist\DXSETUP.exe
/silent` (D3DX 9/10/11 and D3DCompiler_43), and the Visual C++ 2010 SP1
runtime (`MSVCR100.dll`) that the SDK samples import (`apps/dxsetup.txt`).

| Application (pinned in `tests/e2e/PINS`) | API | windows-11 | windows-10 | Modules |
| --- | --- | --- | --- | --- |
| DirectX SDK June 2010 `BasicHLSL.exe`, x64 | D3D9 | **renders**: the animated, lit "tiny" model with DXUT's UI; the overlay reads `HAL (hw vp): Hardsoft VirtIO GPU 3D controller` (`console-d3d9-x64-BasicHLSL.png`) | **renders**, same (`console-d3d9-x64-BasicHLSL.png`) | `d3d9.dll`, `viogpu_d3d10.dll`, D3DX 9 from System32; 0 app-local |
| Same, x86 | D3D9 | **renders**, same, through SysWOW64 (`console-d3d9-x86-BasicHLSL.png`) | **renders**, same | as above, SysWOW64 |
| DirectX SDK `ParallaxOcclusionMapping.exe`, x64 | D3D9 | **renders**: the parallax-occlusion-mapped stone disc, HAL on our device (`console-d3d9-x64-ParallaxOcclusionMapping.png`) | **renders**, same | as above |
| DirectX SDK `BasicHLSL11.exe`, x64 | D3D11 | **renders**: the lit "tiny" model in an 800×600 window on the screen; the overlay reads `HARDWARE: Hardsoft VirtIO GPU 3D controller` (`console-d3d11-x64-BasicHLSL11.png`) | **renders**, same | `d3d11.dll`, `dxgi.dll`, `viogpu_d3d10.dll`, D3DX 11 from System32; 0 app-local |
| DirectX SDK `SubD11.exe`, x64 | D3D11 | **renders**: the tessellated, textured character with the green wire overlay, on `HARDWARE: Hardsoft VirtIO GPU 3D controller` (`console-d3d11-x64-SubD11.png`) | **renders**, same | as above |
| Blender 4.2.3 LTS, `blender.exe --factory-startup` | OpenGL 4.3 | **renders**: the complete Blender UI, 3D viewport, grid and gizmos, with the splash in front (the Esc meant to close it did not reach the window) (`console-opengl-x64-Blender.png`) | **renders, but the window is translucent**: the desktop wallpaper shows through the viewport and panels (`console-opengl-x64-Blender.png`). See "Findings" | `opengl32.dll`, `viogpu_wgl.dll`, `vulkan-1.dll`, `libvulkan_virtio.dll` from System32. The 4 app-local modules are Blender's own `python311.dll`, `python3.dll`, `blender_cpu_check.dll` and `cycles_kernel_oneapi_aot.dll`, none of them a graphics driver |

Every application ran as the logged-on user in session 1 and was still
running when it was closed (`timed_out: true` in `<app>.run.json`). The
module lists are in `<app>.modules.txt`.

**Both D3D11 samples now render in the e2e run on both templates.** The
earlier acceptance runs (`b3e1798`, `cb4d53c`) recorded both as failures.
`docs/d3d11-apps.md` found two causes:

- SubD11 hit three UMD bugs, fixed by `patches/mesa/0015` to `0017`.
- The June 2010 DXUT places BasicHLSL11's window off the screen on Windows
  8 and later unless the SDK's `D3D11Ref.dll` is present. That is an
  application bug ([known limits](known-limits.md)). `tests/e2e/apps.sh`
  installs `D3D11Ref.dll` from the pinned SDK's developer runtime into
  System32 and SysWOW64, as the SDK installer would (`apps/dxsetup.txt`
  gives the hashes).

DXUT loads `D3D11Ref.dll` only while it enumerates devices. It is not in
either sample's module list, and both overlays name the HARDWARE device.

**Not attempted: D3D12.** The candidate was Microsoft's
DirectX-Graphics-Samples `D3D12HelloTriangle` (MIT), built from source with
the project toolchain, llvm-mingw. It does not compile unmodified with that
toolchain:

- The sample calls `ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart()`
  in the MSVC return-value form. DirectX-Headers deliberately declares the
  MinGW-ABI form, with a `RetVal` out-parameter, for non-MSVC Windows
  compilers.
- It uses `Microsoft::WRL::Wrappers::FileHandle`, which mingw's WRL does not
  provide.
- It uses MSVC's `_uuidof`.

Building it unmodified needs MSVC or clang-cl with the Windows SDK. Patching
the source would defeat the purpose, so this stretch item is skipped.

## Findings from these runs

Found by earlier runs, fixed on main, and confirmed by the final runs:

- **vulkaninfo crashed** (0xC0000005) in the package's own
  `vulkaninfo.exe`, x64 and x86, as soon as a Venus device enumerated.
  Vulkan-Tools sets up `User32Handles` only under `_MSC_VER`, so the
  llvm-mingw build dereferenced a null `user32_handles` when it created its
  surface window. LunarG's vulkaninfo worked with the same loader and ICD.
  Fixed by `patches/vulkan-tools/0001`.
- **dxdiag reported DirectDraw "Not Available"**, so `hardware_acceleration`
  was false even though D3D was `Enabled`. Fixed by
  `patches/mesa/0012`–`0014` (the D3D9 UMD's DirectDraw HAL).
- **The DirectX SDK's D3D11 (DXUT11) samples did not show.** Fixed by
  `patches/mesa/0015`–`0017` and explained by DXUT's window placement, as
  described under "Stretch" above. Both now render in the e2e run on both
  templates. The hypothesis recorded here earlier, DXGI output and mode
  enumeration, was wrong (`docs/d3d11-apps.md`).

Open:

- **Winlogon autologon values can vanish across a reboot (cause found,
  guarded).** On fresh windows-11 clones, `AutoAdminLogon` and
  `DefaultUserName` were already gone after `Set-TestDesktop.ps1 -Reboot`,
  before the install ran: in one of the four earlier windows-11 runs and in
  all three windows-11 boots of the AMD final round. vmlab traced it to
  Windows' user OOBE, which deletes those values when it exits and can still
  be running when the clone is reported ready (handoff `20261018-092229`;
  [known limits](known-limits.md#autologon-values-can-vanish-on-a-fresh-clone)).
  `Set-TestDesktop.ps1` now waits for OOBE first. In the four NVIDIA runs
  the wait took 0 to 5 s (`test-desktop.txt`), the values survived
  (`autologon-*.txt`), and the fallback did not fire.
- **D3D9 runs at about 30 fps** on this host (27 to 34 in the final runs),
  against about 60 for the other APIs (the tables above). On Zeus's Intel GPU it reached about 59 fps
  (`docs/zeus.md`). Not investigated ([known limits](known-limits.md)).
- **Blender's window is translucent on Windows 10.** On windows-10 the
  wallpaper shows through Blender's viewport and panels
  (`windows-10/apps/console-opengl-x64-Blender.png`). On windows-11 the same
  build is opaque. The earlier windows-10 run at `cb4d53c` shows the same
  translucency, although this page recorded it then as "renders, same", so
  it is not a regression. The OpenGL probe, which clears with alpha 1, is
  opaque on both templates (`console-x64-opengl_probe.png`). The likely
  cause is that Windows 10's compositor honours the alpha that Blender
  leaves in its framebuffer and Windows 11's does not. That is unconfirmed and not investigated ([known limits](known-limits.md#windows-10)).

Fixed after the earlier runs; the final runs exercise part of the fix:

- **`tests/guest/Run-Probe.ps1`** failed on `-Raw` with no `-ArgumentList`,
  because `New-ScheduledTaskAction` rejects an empty `-Argument`. An exit
  code of 0x80000000 or above, such as 0xC0000135 for a missing DLL, threw an
  Int32 conversion error instead of being reported. Commit `cca767b` passes
  `-Argument` only when there is one, and reinterprets the unsigned task
  result as the signed exit code (run.json also gets `exit_code_hex`). Both
  were checked without a guest, in PowerShell 7.4.2 in Docker: all guest
  scripts parse (`Test-GuestScripts.ps1`), and the conversion maps 0xC0000135
  and 0x80000000 to their signed values where the old cast throws. In the
  final runs, the guest's Windows PowerShell 5.1 ran the fixed script for
  every probe and application, with exit codes 0 and 124. A `-Raw` run with
  no arguments, or an exit code of 0x80000000 or above, did not come up.

## Other hosts

These runs used the AMD iGPU on helios. The same Done checks also pass on an
Intel Iris Xe host (`docs/zeus.md`, 12 of 12 with `patches/mesa/0021`) and
on helios's RTX 5090 (below). [Known limits](known-limits.md#host-gpu-support)
has the matrix.

### NVIDIA RTX 5090

Two `windows-11` runs and one `windows-10` run on vmlab 0.27.0-alpha,
through the user's normal supervisor, with the package built at `08ff884`
(the same `build/`, `installer/`, `probes/`, `patches/` and
`scripts/build.sh` as `4e65d25`):

```sh
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1 --lab win3d-nvidia-e2e-w11-nv1 --apps
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab win3d-nvidia-e2e-w11-nv2 --apps
tests/e2e/run.sh --template windows-10 --host-gpu Nvidia1 --lab win3d-nvidia-e2e-w10-nv1
```

The `Nvidia2` run was repeated on vmlab 0.28.0-alpha once helios's host
config gave `Nvidia2` the same `qemu_binary`, without `--apps` (below):

```sh
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab win3d-nv2-w11
```

Their result tables are above (`windows-10-nvidia1`, `windows-11-nvidia1`,
`windows-11-nvidia2`). **windows-10 on Nvidia1 also passes 12 of 12**, with
the same QEMU, environment and filter line as windows-11 on Nvidia1, one
Venus device, and every render server on `0000:01:00.0`. It ran without
`--apps`, because the stretch applications hang the guest on NVIDIA (below).
The windows-11 table below compares the two cards; the `Nvidia2` column
is the first, stock-QEMU run (`045fda1`, evidence in git history at
`a58acd3`); the paragraph after it has the rerun.

| What | Nvidia1 (`0000:01:00.0`) | Nvidia2 (`0000:05:00.0`) | Evidence |
| --- | --- | --- | --- |
| Host config | `qemu_binary` = the patched QEMU 11.1.1 | no `qemu_binary` | `host.txt` |
| QEMU vmlab ran | `/home/wil/.local/opt/qemu-11.1.1-vmlab-egl/bin/qemu-system-x86_64 ... -L /usr/share/qemu`; `status -v`: `host_gpu=Nvidia1 qemu=<path>` | `qemu-system-x86_64` from `PATH` (the stock 11.1.1), no `-L`; `status -v`: `host_gpu=Nvidia2` | `qemu-argv-env.txt`, `status-v.txt` |
| QEMU environment | `VK_LOADER_DRIVERS_SELECT=*nvidia*`, `VK_ADD_IMPLICIT_LAYER_PATH=...`, `VMLAB_VK_PCI_ADDRESS=0000:01:00.0` | the same with `0000:05:00.0` | `qemu-argv-env.txt` |
| PCI filter | `vmlab-pci-filter: 0000:01:00.0: kept NVIDIA GeForce RTX 5090, hid 1` | `vmlab-pci-filter: 0000:05:00.0: kept NVIDIA GeForce RTX 5090, hid 1` | `qemu-argv-env.txt` |
| Guest vulkaninfo | exactly one device, `Virtio-GPU Venus (NVIDIA GeForce RTX 5090)` | the same | `vulkaninfo/vulkaninfo-x64.txt` |
| Card that rendered | every `virgl_render_server` on `0000:01:00.0` | every `virgl_render_server` on `0000:05:00.0` | `nvidia-smi-during-probes.txt` |
| Probes | all ten pass, 22 fps (D3D9) to 64 fps | all ten render and read back | `probes/` |
| Console | desktop and every probe visible | **black** in every capture | `console-*.png`, `shots.json` |
| Done checks | **12 of 12** | **4 of 12**: the eight D3D and OpenGL checks fail only on "probe window not found in any console capture" | `results.txt` |

**With `qemu_binary`, Nvidia2 passes 12 of 12.** The first run's console
was black for the reason `docs/nvidia.md` gives as fault 2: stock QEMU's
egl-headless cannot show NVIDIA's LINEAR scanout. The rerun at `99ad0c1`
(2026-10-18, vmlab 0.28.0-alpha, the bundle built at `99ad0c1`) used the
patched QEMU: `qemu-argv-env.txt` has `vmlab: argv:` starting with
`/home/wil/.local/opt/qemu-11.1.1-vmlab-egl/bin/qemu-system-x86_64 ... -L
/usr/share/qemu`, `VMLAB_VK_PCI_ADDRESS=0000:05:00.0` and
`vmlab-pci-filter: 0000:05:00.0: kept NVIDIA GeForce RTX 5090, hid 1`;
`status-v.txt` has `host_gpu=Nvidia2 qemu=<path>`; the guest lists one
Venus device, and all ten probes pass on the console at 20 fps (D3D9) to
61 fps. It has no `nvidia-smi` capture, and no `apps/`.

**An earlier Nvidia1 attempt lost its results to a stretch application.**
The first run at `08ff884` passed every probe, then the DirectX SDK's D3D9
`BasicHLSL` x64 hung the guest (host Xid 69, [known
limits](known-limits.md#host-gpu-support)), and the collect step that ran
after the applications found no agent. `run.sh` now collects first
(`045fda1`). The second run hit the same hang at the same point
(`apps/kernel-nvrm.txt`, `apps/console-d3d9-x64-BasicHLSL.png`, which shows
the frozen desktop), after its Done evidence was safe. The first Nvidia2 run
hung the same way, with the Xid on `0000:05:00.0` (its `apps/` is in git
history at `a58acd3`). In both runs the other
five applications then failed at once against the hung guest, so on NVIDIA
`apps/apps.json` is empty and their screenshots show the same frozen (on Nvidia2, black)
frame.

The earlier stock-QEMU Nvidia1 boot on vmlab 0.25 (install and all ten
probes pass, black console) is replaced by these runs; its evidence is in
git history at `c90674b`.

## Evidence index

Every directory under `docs/evidence/`. "Historical" marks evidence from the
vmlab 0.23 era, when labs needed `qemu_args` workarounds (`docs/host.md`). It
is still valid for what it shows, but the package and the lab setup have
changed since.

| Directory | What it proves | vmlab | Package or commit | Status |
| --- | --- | --- | --- | --- |
| [`acceptance/windows-11/`](evidence/acceptance/windows-11/) | Done items 1 and 2 on a fresh `windows-11` lab: build, install, vulkaninfo, the D3D9, D3D11, D3D12, OpenGL and Vulkan probes on the console, dxdiag; the stretch apps in `apps/` | 0.25.0-alpha, `host_gpu = "AMD1"` | `6105889`, built by the run | Current |
| [`acceptance/windows-10/`](evidence/acceptance/windows-10/) | The same on a fresh `windows-10` lab | 0.25.0-alpha, `host_gpu = "AMD1"` | `6105889`, reusing the same bundle | Current |
| [`acceptance/windows-11-nvidia1/`](evidence/acceptance/windows-11-nvidia1/) | Done items 1 and 2 on a fresh `windows-11` lab on the first RTX 5090 with the patched QEMU as its `qemu_binary`: 12 of 12, one Venus device; the D3D9 `BasicHLSL` hang in `apps/` | 0.27.0-alpha, `host_gpu = "Nvidia1"` | `045fda1`, reusing the bundle built at `08ff884` | Current |
| [`acceptance/windows-10-nvidia1/`](evidence/acceptance/windows-10-nvidia1/) | Done items 1 and 2 on a fresh `windows-10` lab on the first RTX 5090 with the patched QEMU: 12 of 12, one Venus device | 0.27.0-alpha, `host_gpu = "Nvidia1"` | `a58acd3`, same bundle | Current |
| [`acceptance/windows-11-nvidia2/`](evidence/acceptance/windows-11-nvidia2/) | Done items 1 and 2 on a fresh `windows-11` lab on the second RTX 5090 with the patched QEMU as its `qemu_binary`: 12 of 12, one Venus device, the PCI filter on `0000:05:00.0`. The earlier stock-QEMU run (black console, 4 of 12, the `BasicHLSL` hang in `apps/`) is in git history at `a58acd3` | 0.28.0-alpha, `host_gpu = "Nvidia2"` | `99ad0c1`, reusing the bundle built at `99ad0c1` | Current |
| [`vulkaninfo/`](evidence/vulkaninfo/README.md) | The package's patched `vulkaninfo.exe` (`patches/vulkan-tools/0001`) lists Venus and exits 0, x64 and x86 | 0.24.0-alpha | `78fc688` (`work/fix-vulkaninfo`) | Current |
| [`dxdiag/`](evidence/dxdiag/) | dxdiag's DDraw and AGP status before and after `patches/mesa/0012` to `0014`, with `ddcaps` DirectDraw checks ([`dxdiag.md`](dxdiag.md)) | 0.24.0-alpha | before: `main` at `be2ef9b`; after: `deb2585` | Current |
| [`d3d11-apps/`](evidence/d3d11-apps/) | SubD11 and BasicHLSL11: the causes, `patches/mesa/0015` to `0017`, the DXUT window bug and the `D3D11Ref.dll` workaround ([`d3d11-apps.md`](d3d11-apps.md)) | 0.24.0-alpha | `5fd206d` | Current |
| [`nvidia/`](evidence/nvidia/) | The two RTX 5090 faults and their fixes, `patches/mesa/0020` and `patches/host/qemu/0001`; `05-amd-regression/` shows no AMD regression ([`nvidia.md`](nvidia.md)) | 0.24.0-alpha, isolated supervisor | `bea2a93` | Current for the diagnosis; `01-` to `03-` are diagnostic steps. The acceptance runs above replace `04-guest-fix/` as the proof that NVIDIA works |
| [`zeus/`](evidence/zeus/) | Intel Iris Xe fails on `main` and passes with `patches/mesa/0021`; the AMD regression run with 0021; the RTX 4060 failure ([`zeus.md`](zeus.md)) | 0.24.0-alpha | `main` at `db07da3`; 0021 at `1bbbfe8` | Current |
| [`package/`](evidence/package/README.md) | A fresh-clone build, then install, probes, rerun, uninstall back to the baseline and reinstall, on a fresh `windows-11` lab | 0.23, with workarounds (`lab-vmlab.wcl`) | `f7fa236` (`work/package`) | **Historical.** The bundle predates `patches/vulkan-tools/0001` and Mesa 0012 on, so the file hashes it quotes are not today's. The uninstall evidence is still the only uninstall run |
| [`kmd/`](evidence/kmd/README.md) | The KMD's test signature is accepted and the driver loads, with no Code Integrity events | 0.23 (`display = "virtio-vga-gl"`) | `e166465` | **Historical**: a kernel-only install with no UMDs, so the device showed problem code 43; the full package's device has problem code 0 (`acceptance/*/installed-gpu-state.json`). Signing is unchanged since |
| [`probes/`](evidence/probes/README.md) | The probes and guest scripts work on Microsoft's software renderers, before any driver from this repository | 0.23, no GPU | `26ca683` | **Historical** |
| [`host/`](evidence/host/) | The host-probe captures `host.md` cites: which QEMU, display and loader settings Venus needs | 0.23, with `qemu_args` variants | `d1bad91` | **Historical**: vmlab 0.24 emits these settings itself |

## Done item 3

The script does not check item 3. These are the documents that answer it:

| Topic | Document |
| --- | --- |
| Install | `docs/install.md` |
| Uninstall | `docs/uninstall.md` (the run that proves it: `docs/evidence/package/`) |
| Component stack and upstream pins | `docs/stack.md` (pin and patch tables generated from `build/*/PINS`, `patches/host/PINS` and `patches/`; `scripts/pins-table.py --check`) and `build/*/README.md` |
| Signing decision | `docs/decisions/0001-signing.md` |
| Known limits | `docs/known-limits.md`, including the host GPU matrix |
| Evidence for item 2 | This page, the evidence index above, and the investigations it links: `docs/dxdiag.md`, `docs/d3d11-apps.md`, `docs/nvidia.md`, `docs/zeus.md` |

`scripts/check-docs.py` checks that every repository path named in `docs/`
and `README.md` exists.
