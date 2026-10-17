# Acceptance

This page checks the "Done" criteria in `docs/brief.md` one by one, on fresh
vmlab labs from the local `windows-11` and `windows-10` templates. One script
produces all of the evidence, and anyone can run it again:

```sh
tests/e2e/run.sh --template windows-11     # or windows-10; --keep leaves the lab up
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
   (`win3d-e2e-<template>`), and runs `vmlab up`. The lab has the
   `labs/probe/vmlab.wcl` shape for vmlab 0.24: `secure_boot = false` and
   `gpu { mode = "vulkan" address = "0000:72:00.0" }` (the AMD iGPU). It also
   sets `agent_update = false`, because the templates' agent cannot be
   updated in place and a late agent restart broke one windows-10 run.
   `qemu-argv-env.txt` keeps the QEMU command line and environment that vmlab
   logged.
3. **Baseline.** Copies `tests/guest/`, `tests/e2e/guest/` and `out/probes/`
   into the guest and records `tests/guest/Get-GpuState.ps1`
   (`baseline-gpu-state.json`). It then creates the autologon test user with
   `tests/guest/Set-TestDesktop.ps1 -Reboot`.
4. **Install.** Runs the documented command from `docs/install.md`,
   `scripts/install-into-vm.sh <lab-dir> <vm>`, unchanged (`install.log`,
   `install-journal.log`, `installed-gpu-state.json`). The script then checks
   that autologon survived the install's restarts (`autologon-*.txt`). If it
   did not, the script runs `Set-TestDesktop.ps1 -Reboot` again.
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
   them (`results.json`, `results.txt`).
7. **Teardown.** Runs `vmlab destroy`, unless `--keep` was given.

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
vulkaninfo, the LunarG cross-check, the Vulkan probes, the device state after
install, and dxdiag's Direct3D status on its own.

## Host stack

Each run records the host in `docs/evidence/acceptance/<template>/host.txt`.
For the runs below:

| Item | Value |
| --- | --- |
| Host GPU used for Venus | AMD Granite Ridge iGPU (Radeon Graphics), PCI `0000:72:00.0`, `/dev/dri/renderD129`, RADV |
| QEMU | 11.1.1 (`qemu-full 11.1.1-1`) |
| virglrenderer | 1.3.0 (`1.3.0-2.1`) |
| Mesa / RADV | 26.2.2 (`mesa 3:26.2.2-2`, `vulkan-radeon 3:26.2.2-2`) |
| vmlab | 0.24.0-alpha |
| Kernel | 7.2.3-1-cachyos |

vmlab 0.24 started QEMU with `-device
virtio-vga-gl,venus=true,blob=true,hostmem=4G -display
egl-headless,rendernode=/dev/dri/renderD129` and `VK_LOADER_DRIVERS_SELECT=*radeon*`
in its environment (`qemu-argv-env.txt`). That matches the vmlab 0.23
workaround in `docs/host.md`, and the guest behaved the same.

## Results

**Both templates pass every Done check in item 2, and item 1.** Commits
tested:

- **windows-11**: `b3e1798`. That is main at `db07da3` (with the vulkaninfo,
  dxdiag DirectDraw and NVIDIA feedback-loop fixes) merged into this branch.
  This run built the package with `scripts/build.sh` (`build.txt`, mode
  `built`).
- **windows-10**: `cb4d53c`. It differs from `b3e1798` only in two one-line
  `tests/e2e/` fixes and the windows-11 evidence, so it reused the same
  `out/package/` bundle (`build.txt`, mode `reused`, built at `b3e1798`).

<!-- e2e:results -->
### windows-11

Run 2026-10-17T17:00:52+10:00 at commit `b3e179837e76`, template `x86_64/windows-11@26100.1742.5`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-11/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | built; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 30.17 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 34.39 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.91 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.87 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.92 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.36 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 65.82 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 66.35 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 58.92 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.82 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-10

Run 2026-10-17T17:25:18+10:00 at commit `cb4d53c67556`, template `x86_64/windows-10@19045.2006.8`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-10/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 29.83 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 29.41 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.96 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.69 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.97 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.31 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 67.67 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 67.57 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.92 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.89 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
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
| Same, x86 | D3D9 | **renders**, same, through SysWOW64 (`console-d3d9-x86-BasicHLSL.png`) | **renders** | as above, SysWOW64 |
| DirectX SDK `ParallaxOcclusionMapping.exe`, x64 | D3D9 | **renders**: the parallax-occlusion-mapped stone disc, about 49 fps, HAL on our device (`console-d3d9-x64-ParallaxOcclusionMapping.png`) | **renders** | as above |
| DirectX SDK `BasicHLSL11.exe`, x64 | D3D11 | **fails**: the process runs for 30 s and its button is on the taskbar, but no window ever appears (`console-d3d11-x64-BasicHLSL11.png`). Explained and worked around since; see below | **fails**, same | `d3d11.dll`, `dxgi.dll`, `viogpu_d3d10.dll`, D3DX 11 from System32; 0 app-local |
| DirectX SDK `SubD11.exe`, x64 | D3D11 | **fails**: DXUT's teardown dialog, "The Direct3D device has a non-zero reference count", with no rendered frame (`console-d3d11-x64-SubD11.png`). Fixed since; see below | **fails**, same | as above |
| Blender 4.2.3 LTS, `blender.exe --factory-startup` | OpenGL 4.3 | **renders**: the complete Blender UI, 3D viewport, grid and gizmos, with the splash in front (the Esc meant to close it did not reach the window) (`console-opengl-x64-Blender.png`) | **renders**, same | `opengl32.dll`, `viogpu_wgl.dll`, `vulkan-1.dll`, `libvulkan_virtio.dll` from System32. The 4 app-local modules are Blender's own `python311.dll`, `python3.dll`, `blender_cpu_check.dll` and `cycles_kernel_oneapi_aot.dll`, none of them a graphics driver |

Every application ran as the logged-on user in session 1 and was still
running when it was closed (`timed_out: true` in `<app>.run.json`). The
module lists are in `<app>.modules.txt`.

**After these runs, both D3D11 samples were made to render.** The table above
is the state at the acceptance commits. `docs/d3d11-apps.md` found two
causes:

- SubD11 hit three UMD bugs, fixed by `patches/mesa/0015` to `0017`. In a
  fresh Windows 11 lab with the package at `5fd206d`, it renders on the
  hardware device.
- BasicHLSL11 renders, but the June 2010 DXUT places its window off the
  screen on Windows 8 and later unless the SDK's `D3D11Ref.dll` is present.
  That is an application bug ([known limits](known-limits.md)).

`tests/e2e/apps.sh` now installs `D3D11Ref.dll` from the pinned SDK's
developer runtime, as the SDK installer would, so that BasicHLSL11's window
is on the screen (commit `3bf8e4c`). **That change has not been re-run yet**;
the evidence for it is the manual run in `docs/d3d11-apps.md`
(`console-d3d11-x64-BasicHLSL11-with-D3D11Ref.png`).

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

Found by earlier runs, fixed on main before the final runs:

- **vulkaninfo crashed** (0xC0000005) in the package's own
  `vulkaninfo.exe`, x64 and x86, as soon as a Venus device enumerated.
  Vulkan-Tools sets up `User32Handles` only under `_MSC_VER`, so the
  llvm-mingw build dereferenced a null `user32_handles` when it created its
  surface window. LunarG's vulkaninfo worked with the same loader and ICD.
  Fixed by `patches/vulkan-tools/0001`.
- **dxdiag reported DirectDraw "Not Available"**, so `hardware_acceleration`
  was false even though D3D was `Enabled`. Fixed by
  `patches/mesa/0012`–`0014` (the D3D9 UMD's DirectDraw HAL).

Open:

- **Winlogon autologon values can vanish across a reboot.** On a fresh
  windows-11 clone (one of the four windows-11 runs), `AutoAdminLogon` and `DefaultUserName`
  were already gone after `Set-TestDesktop.ps1 -Reboot`, before the install
  ran. `DefaultPassword` survived. The installer does not touch Winlogon,
  and vmlab's repository has no code that does. The template agent's
  first-logon hooks are the likely cause, but this is unconfirmed. `run.sh`
  records the values before and after the install (`autologon-*.txt`) and
  sets them again when they are missing.
- **D3D9 runs at about 30 fps** on this host, against about 60 for the
  other APIs (the tables above). On Zeus's Intel GPU it reached about 59 fps
  (`docs/zeus.md`). Not investigated ([known limits](known-limits.md)).

Found by these runs and fixed after them, not yet re-run in a guest:

- **The DirectX SDK's D3D11 (DXUT11) samples did not show**: fixed or
  explained as described under "Stretch" above. The hypothesis recorded here
  earlier, DXGI output and mode enumeration, was wrong
  (`docs/d3d11-apps.md`).
- **`tests/guest/Run-Probe.ps1`** failed on `-Raw` with no `-ArgumentList`,
  because `New-ScheduledTaskAction` rejects an empty `-Argument`. An exit
  code of 0x80000000 or above, such as 0xC0000135 for a missing DLL, threw an
  Int32 conversion error instead of being reported. Commit `cca767b` passes
  `-Argument` only when there is one, and reinterprets the unsigned task
  result as the signed exit code (run.json also gets `exit_code_hex`). Both
  were checked without a guest, in PowerShell 7.4.2 in Docker: all guest
  scripts parse (`Test-GuestScripts.ps1`), and the conversion maps 0xC0000135
  and 0x80000000 to their signed values where the old cast throws. Windows
  PowerShell 5.1, which the guest runs, was not available.

## Other hosts

These runs used the AMD iGPU on helios. The same Done checks also pass on an
Intel Iris Xe host (`docs/zeus.md`, 12 of 12 with `patches/mesa/0021`).
NVIDIA works only in part. [Known limits](known-limits.md#host-gpu-support)
has the matrix.

## What main contains beyond these runs

The acceptance runs tested the package at `b3e1798` and `cb4d53c`, with Mesa
patches 0001 to 0014 and 0020. Since then, `main` has gained
`patches/mesa/0015` to `0017` (D3D11 samples, tested on Windows 11 at
`5fd206d`) and `patches/mesa/0021` (Intel, tested on Windows 11 on Intel and
AMD at `1bbbfe8`), each tested without the other. The full series builds
(`build/mesa/README.md`, "Reproducibility"), but `tests/e2e/run.sh` has not
run on it. Re-running it on both templates is the check that would cover
that.

## Evidence index

Every directory under `docs/evidence/`. "Historical" marks evidence from the
vmlab 0.23 era, when labs needed `qemu_args` workarounds (`docs/host.md`). It
is still valid for what it shows, but the package and the lab setup have
changed since.

| Directory | What it proves | vmlab | Package or commit | Status |
| --- | --- | --- | --- | --- |
| [`acceptance/windows-11/`](evidence/acceptance/windows-11/) | Done items 1 and 2 on a fresh `windows-11` lab: build, install, vulkaninfo, the D3D9, D3D11, D3D12, OpenGL and Vulkan probes on the console, dxdiag; the stretch apps in `apps/` | 0.24.0-alpha | `b3e1798`, built by the run | Current |
| [`acceptance/windows-10/`](evidence/acceptance/windows-10/) | The same on a fresh `windows-10` lab | 0.24.0-alpha | `cb4d53c`, reusing the `b3e1798` bundle | Current |
| [`vulkaninfo/`](evidence/vulkaninfo/README.md) | The package's patched `vulkaninfo.exe` (`patches/vulkan-tools/0001`) lists Venus and exits 0, x64 and x86 | 0.24.0-alpha | `78fc688` (`work/fix-vulkaninfo`) | Current |
| [`dxdiag/`](evidence/dxdiag/) | dxdiag's DDraw and AGP status before and after `patches/mesa/0012` to `0014`, with `ddcaps` DirectDraw checks ([`dxdiag.md`](dxdiag.md)) | 0.24.0-alpha | before: `main` at `be2ef9b`; after: `deb2585` | Current |
| [`d3d11-apps/`](evidence/d3d11-apps/) | SubD11 and BasicHLSL11: the causes, `patches/mesa/0015` to `0017`, the DXUT window bug and the `D3D11Ref.dll` workaround ([`d3d11-apps.md`](d3d11-apps.md)) | 0.24.0-alpha | `5fd206d` | Current |
| [`nvidia/`](evidence/nvidia/) | The two RTX 5090 faults and their fixes, `patches/mesa/0020` and `patches/host/qemu/0001`; `05-amd-regression/` shows no AMD regression ([`nvidia.md`](nvidia.md)) | 0.24.0-alpha, isolated supervisor | `bea2a93` | Current; `01-` to `03-` are diagnostic steps |
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
