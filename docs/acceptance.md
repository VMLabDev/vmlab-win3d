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
| DirectX SDK `BasicHLSL11.exe`, x64 | D3D11 | **fails**: the process runs for 30 s and its button is on the taskbar, but no window ever appears (`console-d3d11-x64-BasicHLSL11.png`) | **fails**, same | `d3d11.dll`, `dxgi.dll`, `viogpu_d3d10.dll`, D3DX 11 from System32; 0 app-local |
| DirectX SDK `SubD11.exe`, x64 | D3D11 | **fails**: DXUT's teardown dialog, "The Direct3D device has a non-zero reference count", with no rendered frame (`console-d3d11-x64-SubD11.png`) | **fails**, same | as above |
| Blender 4.2.3 LTS, `blender.exe --factory-startup` | OpenGL 4.3 | **renders**: the complete Blender UI, 3D viewport, grid and gizmos, with the splash in front (the Esc meant to close it did not reach the window) (`console-opengl-x64-Blender.png`) | **renders**, same | `opengl32.dll`, `viogpu_wgl.dll`, `vulkan-1.dll`, `libvulkan_virtio.dll` from System32. The 4 app-local modules are Blender's own `python311.dll`, `python3.dll`, `blender_cpu_check.dll` and `cycles_kernel_oneapi_aot.dll`, none of them a graphics driver |

Every application ran as the logged-on user in session 1 and was still
running when it was closed (`timed_out: true` in `<app>.run.json`). The
module lists are in `<app>.modules.txt`.

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
- **The DirectX SDK's D3D11 (DXUT11) samples do not start**: see the table
  above. They load the system `d3d11.dll`, `dxgi.dll` and our
  `viogpu_d3d10.dll`, but they never show a rendering window. The D3D11
  probes pass. One unverified hypothesis: DXUT11 enumerates DXGI outputs and
  display modes, which the probe never does.
- `tests/guest/Run-Probe.ps1`: `-Raw` with no `-ArgumentList` fails, because
  `New-ScheduledTaskAction` rejects an empty `-Argument`. An exit code of
  0x80000000 or above, such as 0xC0000135 for a missing DLL, throws an
  Int32 conversion error instead of being reported.

## Done item 3

The script does not check item 3. These are the documents that answer it:

| Topic | Document |
| --- | --- |
| Install | `docs/install.md` |
| Uninstall | `docs/uninstall.md` |
| Component stack and upstream pins | `docs/stack.md` (pin tables generated from `build/*/PINS`) |
| Signing decision | `docs/decisions/0001-signing.md` |
| Known limits | `docs/known-limits.md` |
| Evidence for item 2 | This page and `docs/evidence/acceptance/` |
