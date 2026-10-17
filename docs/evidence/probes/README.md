# Evidence: probes on Microsoft's software renderers

This run proves that the probes and guest scripts work before any driver from
this repository is involved. It used a throwaway lab, `win3d-probes-1`: one VM
from the local `windows-11` template (26100.1742.5), 4 vCPUs, 8 GiB, no
`gpu {}` block and no NIC. The lab was destroyed afterwards. In that VM the
display device is the template's **Red Hat VirtIO GPU DOD controller**
(`viogpudo`, a display-only WDDM 1.3 driver). Direct3D therefore runs on the
**Microsoft Basic Render Driver** (WARP, `d3d10warp.dll`), and OpenGL runs on
**GDI Generic**. No Vulkan ICD is registered.

Date: 2026-10-17. Probe binaries: `build/probes/build.sh` at the commit that
adds this file. A clean rebuild is bit-identical. SHA256 of the binaries:

```
fc76befac4c765bece8c6d1fea7469f7126ab93efeb72782a3bbbbbb7876f466  x64/d3d11_probe.exe
a004feae31e0680c05dd4b502b292f3ae0a9d1530d003e87e71d5f175729c459  x64/d3d12_probe.exe
bd91307b676e685cae312c2be3f3f7ef10681668d92c6288b23d32c11a895700  x64/d3d9_probe.exe
6f664aed1667bce0ceab4dbb9ea685f73dc04b94a827c83b02fb917ae7da6fed  x64/opengl_probe.exe
985f86f7010ae440c6aeadb00fc053dc063f11e37f5bfc3ba6576845fe8fcb81  x64/vulkan_probe.exe
b547eaa23105f2bfb978e0fe50c7a7956acc081617ce8048e789d804503b5a1b  x86/d3d11_probe.exe
accabdfd29d61b069a4a0195d3855bb28ae9b88a47207b93085065289b1f7fc4  x86/d3d12_probe.exe
2f6d03c673ee10ed2515187244a3249f3d01cac171e87b6d71d4ebb92dfc05ab  x86/d3d9_probe.exe
071f8a27dfca14f6a4625e8a341104b1fcf5cf6cfde92002ea9dc612996d9c6f  x86/opengl_probe.exe
ba03f77f35cdfb15c80a1233d26d15a91cc808fd28cecdc53d3bf5d0026709cf  x86/vulkan_probe.exe
```

## Steps

1. `vmlab up`, then copy `tests/guest/` and `out/probes/` to the guest with
   `vmlab cp`.
2. `Set-TestDesktop.ps1 -Reboot` (as SYSTEM): autologon user `win3d`.
3. `Run-AllProbes.ps1 -Seconds 15 -Warp`: every probe in session 1 through a
   scheduled task.
4. `Get-Dxdiag.ps1 -AsUser -Xml` and `Get-GpuState.ps1`.
5. While `Run-Probe.ps1` ran the x64 D3D12 probe, the host captured the
   console with `vmlab vm screenshot w11 <png>`, the QMP framebuffer. The
   Start menu from the first logon covered the window at first, so a
   `vmlab vm click` on the desktop dismissed it.

## Results (`results/summary.json`)

| Run | Exit | Result | Adapter | Software | Readback | FPS |
| --- | --- | --- | --- | --- | --- | --- |
| x64 d3d9 | 0 | pass | Microsoft Basic Render Driver | yes | ok | 55.7 |
| x64 d3d11 | 0 | pass | Microsoft Basic Render Driver | yes | ok | 46.5 |
| x64 d3d11 `--warp` | 0 | pass | Microsoft Basic Render Driver | yes | ok | 47.8 |
| x64 d3d12 | 0 | pass | Microsoft Basic Render Driver | yes | ok | 44.5 |
| x64 d3d12 `--warp` | 0 | pass | Microsoft Basic Render Driver | yes | ok | 48.4 |
| x64 opengl | 0 | pass | GDI Generic | yes | ok | 1785 (no vsync) |
| x64 vulkan | 1 | api_failure | — | — | — | — |
| x86 d3d9 | 0 | pass | Microsoft Basic Render Driver | yes | ok | 56.9 |
| x86 d3d11 | 0 | pass | Microsoft Basic Render Driver | yes | ok | 45.0 |
| x86 d3d11 `--warp` | 0 | pass | Microsoft Basic Render Driver | yes | ok | 47.9 |
| x86 d3d12 | 0 | pass | Microsoft Basic Render Driver | yes | ok | 47.2 |
| x86 d3d12 `--warp` | 0 | pass | Microsoft Basic Render Driver | yes | ok | 45.2 |
| x86 opengl | 0 | pass | GDI Generic | yes | ok | 1531 (no vsync) |
| x86 vulkan | 1 | api_failure | — | — | — | — |

Every run was in session 1 as `win3d`. The x86 Vulkan row of `summary.json`
shows a null session because that probe exited after about 50 ms, before
`Run-Probe.ps1` found the process. `Run-Probe.ps1` now falls back to the
probe's own `session_id`; the rerun is in `results/x86-vulkan_probe.run.json`. Readback found exactly
`[255,0,0]`, `[0,0,255]` and `[0,255,0]` at the three markers in every passing
run.

- **D3D9** used `Direct3DCreate9Ex` and a HAL device with hardware vertex
  processing and shader model 3.0. Its D3D9 UMD is `d3d10warp.dll`.
- **D3D11** got feature level 11_0 and a `FLIP_DISCARD` swap chain.
- **D3D12** on the default adapter (`NULL`) got the Basic Render Driver at
  max feature level 12_1, shader model 6.6. `--warp` gives the same adapter
  through `EnumWarpAdapter`.
- **OpenGL** got the GDI generic pixel format, GL 1.1.0, and
  `icd_loaded: false`. GDI generic has no swap interval, so the probe runs
  unthrottled. A driver ICD with `WGL_EXT_swap_control` runs at the refresh
  rate.
- **Vulkan** fails cleanly, as expected. The template ships
  `System32\vulkan-1.dll` 1.3.301 with no ICD registered, so the probe exits
  1 with `"instance lacks VK_KHR_surface (no Vulkan ICD with Win32 WSI is
  registered)"`. See `results/x64-vulkan_probe.json`.
- **Modules.** `results/*.run.json` `graphics_modules` lists only System32 or
  SysWOW64 DLLs: `d3d9`, `d3d11`, `d3d12`, `D3D12Core`, `D3DSCache`, `dxgi`,
  `dxcore`, `d3d10warp`, `dxilconv`, `d3dcompiler_47`, `opengl32`, `glu32`,
  `dwmapi`. The Vulkan runs list none, because they exit before the first
  module sample. `app_local_modules` is
  empty for every run. `results/x86-d3d12_probe.modules.json` shows the
  32-bit modules coming from `C:\WINDOWS\SysWOW64` through Toolhelp
  `SNAPMODULE32`. `results/x64-opengl_probe.modules.json` is the full module
  list for the GDI generic GL run.

## Console screenshot

`console-d3d12-x64.png`: the x64 D3D12 probe on the guest console. It shows the
title "DIRECT3D 12", the pinwheel, the three markers and "X64 - MICROSOFT BASIC
RENDER DRIVER". The image was reduced to 128 colours to keep it small.

## dxdiag (`dxdiag.json`)

| Field | Value |
| --- | --- |
| Card name | Red Hat VirtIO GPU DOD controller |
| Device Type | Display-Only Device (POST) |
| Vendor ID / Device ID | 0x1414 / 0x008C (Microsoft Basic Render Driver) |
| Driver | `viogpudo.sys` 100.101.104.28500, WDDM 1.3 |
| DDI Version | 12 |
| Feature Levels | 12_1,12_0,11_1,11_0,10_1,10_0,9_3,9_2,9_1,1_0_CORE |
| DDraw / D3D / AGP Status | Enabled / Enabled / Enabled |
| `hardware_acceleration` (extract) | **false**: display-only and Basic Render |

Finding for the Done criterion "dxdiag reports hardware acceleration": dxdiag
says `D3D Status: Enabled` even here, with no 3D driver at all. Those fields
alone are not evidence. The e2e slice must also check that the device is not
display-only and not vendor 0x1414/device 0x008C. It should also check that
the driver and DDI are the package's. `Get-Dxdiag.ps1` does this in
`summary.hardware_acceleration`.

## GPU state (`results/gpu-state.json`)

- Display device: `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01`, status OK,
  problem code 0, `oem7.inf`, `VioGpuDod_Inst`, 100.101.104.28500, Red Hat.
- Class key: no `UserModeDriverName` or `OpenGLDriverName` values.
- No Khronos Vulkan drivers or layers in either registry view.
- Boot: `testsigning` is absent, and **Secure Boot is on** in this template
  (`Confirm-SecureBootUEFI` returns true).
