# Build plan

This plan comes from `docs/brief.md` and what `docs/prior-art.md` proves. The
orchestrator owns it and updates it as slices land.

## Target stack

| Layer | Component | Source | Registered by |
| --- | --- | --- | --- |
| Kernel | Yttrium viogpu3d WDDM KMD (Venus, blob) | `arehnman/yttrium-virtio-gpu` @ tag `yttrium-experimental-2026-09-27` (`b99d951a`) + patches | PnP INF |
| Vulkan | Mesa Venus ICD `libvulkan_virtio.dll` | `arehnman/virtio-win-mesa` @ `374c3022` + patches | INF: `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` (+WOW6432Node) |
| Vulkan loader | Khronos `vulkan-1.dll`, `vulkaninfo.exe` | Khronos Vulkan-Loader / Vulkan-Tools, built from source | Installer (System32/SysWOW64), only if absent or older |
| D3D9/10/11 | Yttrium Gallium UMD `viogpu_d3d10.dll` (Venus) | Same Mesa tree | INF: `UserModeDriverName[Wow]` |
| OpenGL | Mesa WGL + Zink `viogpu_wgl.dll` on the Venus ICD | Same Mesa tree | INF: `OpenGLDriverName[Wow]` |
| D3D12 | vkd3d-proton + DXVK DXGI, behind a `d3d12.dll` bridge | vkd3d-proton, dxil-spirv, DXVK, all pinned + codex bridge patch | Installer: replaces System32/SysWOW64 `d3d12.dll`, with backup and a repair task |

Why the D3D9–11 row is not DXVK: Mesa's Win32 Vulkan WSI presents through the
**system** `dxgi.dll` and `d3d11.dll`. Replacing those system-wide with DXVK hung
even vkcube (prior-art §3, §4.5). Registering the UMD through the KMD is also
the supported Windows mechanism, and it is what makes `dxdiag` report
acceleration. Decided by the user (D1, 2026-10-17): use the existing Yttrium drivers where they cover an API, and cover anything missing with Zink and DXVK (with vkd3d-proton for D3D12).

## Host assumptions

- Venus works on the AMD iGPU (RADV, `renderD129`), not on the RTX 5090.
- vmlab >= 0.24.0-alpha (handoff `20261017-142905`, answered 2026-10-17)
  gives a one-adapter `virtio-vga-gl,venus=true,blob=true` device on the
  named host GPU, with the Vulkan ICD filtered in QEMU's own environment, and
  screenshots over VNC. The v0.23 `qemu_args` and isolated-supervisor
  workaround is superseded.
- vmlab >= 0.25.0-alpha (handoff `20261017-210821`, 2026-10-17) moves the
  GPU's PCI address into the host config, `~/.config/vmlab/config.wcl`. Labs
  say `gpu { mode = "vulkan" host_gpu = "AMD1" }` (`labs/probe/vmlab.wcl`),
  and helios's host config declares `gpu "AMD1" { address = "0000:72:00.0" }`.
  The host's default GPU is `Nvidia1` (an RTX 5090), so labs name `AMD1`.
  The evidence so far was taken on 0.23 or 0.24, with
  `gpu { mode = "vulkan" address = "0000:72:00.0" }` on 0.24.

## Build layout contract

Every component has `build/<component>/build.sh`, which runs in Docker
(non-root, no sudo) and writes only to `out/<component>/`. Pins live in
`build/<component>/PINS` (name, URL, tag/commit, sha256). The toolchain is
llvm-mingw `20261006` (LLVM 23.1.3), ucrt, x86_64 Linux host build. 
`scripts/build.sh` (orchestrator-owned) runs them all and assembles the package.

```
out/mesa/{x64,x86}/      viogpu_d3d10.dll viogpu_wgl.dll libvulkan_virtio.dll
out/vulkan-sdk/{x64,x86}/ vulkan-1.dll vulkaninfo.exe
out/d3d12/{x64,x86}/     d3d12.dll (bridge) vkd3dcore.dll dxgi-vulkan.dll
out/kmd/x64/             viogpu3d.sys viogpu3d.inx-derived inf, helper exes
out/probes/{x64,x86}/    test applications
out/package/             the signed install bundle (later slice)
```

## Slices

Wave 1 (parallel):

| Worker | Delivers |
| --- | --- |
| `host-probe` | Minimum QEMU/vmlab line for Venus on this host, console screenshot method, template Secure Boot state; vmlab handoff draft |
| `mesa-build` | Docker build of Mesa (x64, x86) and the Vulkan loader and vulkaninfo; codex Mesa fixes re-exported as patches |
| `d3d12-build` | Docker build of vkd3d-proton, dxil-spirv, the DXGI-only DXVK, and the bridge |
| `kmd-build` | KMD build and test signing, scripted from this repo; the signing decision record |
| `probes` | Small D3D9/D3D11/D3D12/OpenGL/Vulkan test apps, cross-built, that prove GPU rendering, plus guest test scripts |

Wave 3, after the AMD iGPU path passes Done: make Venus work on the host's
NVIDIA RTX 5090 (prior-art §3 dwm crash, decoder errors).
Status: done in part. The RTX 5090 works with `patches/mesa/0020` and a
patched QEMU (`docs/nvidia.md`); an Intel host was added and passes with
`patches/mesa/0021` (`docs/zeus.md`). `docs/known-limits.md` has the host
matrix.

Wave 2: `package` (INF, catalog, installer and uninstaller, `scripts/build.sh`),
then `e2e` (fresh win11 lab, install, evidence for every Done item), then
`docs`. Windows 10 runs after Windows 11 passes.

## Open decisions

- D1 (decided): Yttrium first; Zink and DXVK/vkd3d-proton fill gaps.
- D3 (decided): no host system updates during this work; the host stack stays
  QEMU 11.1.1, virglrenderer 1.3.0, Mesa 26.2.2.
- D2: the signing approach (recorded by `kmd-build` in `docs/decisions/`).
- D4 (decided 2026-10-18): our Mesa, vkd3d-proton, DXVK and Vulkan-Tools
  patches are not sent upstream for now. We keep them as our own maintained
  patch series (and later as forks), rebased when we move pins. QEMU is the
  exception: its fix lives on the VMLabDev/qemu branch for upstreaming.
- D5 (decided 2026-10-19): the user enables Resizable BAR in helios's firmware
  (evening of 2026-10-19). Before the reboot the orchestrator stops cleanly
  (merge finished workers, others commit WIP and destroy labs, release GPU
  slots, write `.scratch/workers/RESUME.md`). After it: verify BAR1 is about
  32 GiB on both RTX 5090s, rerun the NVIDIA acceptance, and raise the slot
  limit to three VMs per card.
