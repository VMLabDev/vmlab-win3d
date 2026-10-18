# vmlab-win3d

A driver package for Windows guests running under vmlab. Install it once in
a Windows 10 or 11 guest, and unmodified Direct3D 9, 10, 11 and 12, OpenGL
and Vulkan applications render on the host's GPU. No application needs local
DLLs, overrides or settings.

The rendering runs on the Vulkan driver of the host running QEMU. The guest
reaches it through virtio-gpu Venus, vmlab's `gpu { mode = "vulkan" }`:

- a WDDM kernel driver with Venus support (Yttrium viogpu3d);
- Mesa's Venus Vulkan ICD;
- Direct3D 9 to 11 through the Yttrium Mesa UMD, OpenGL through Mesa Zink,
  and Direct3D 12 through vkd3d-proton behind a system `d3d12.dll` bridge.

The host side is the host's own QEMU, virglrenderer and Vulkan driver.

The guest package is built from pinned upstream sources by scripts in this
repository, in Docker on Linux. [docs/stack.md](docs/stack.md) follows a
frame through every layer.

## Status

Alpha, for lab VMs only. The package is test-signed and needs Secure Boot
off.

- **Done criteria met on AMD.** On fresh vmlab `windows-11` and `windows-10`
  labs on an AMD host (RADV), one script builds the package, one command
  installs it, `vulkaninfo` lists the Venus device, the D3D9, D3D11, D3D12,
  OpenGL and Vulkan probes (x64 and x86) render on the GPU and show on the
  console, and dxdiag reports hardware acceleration
  ([acceptance](docs/acceptance.md)).
- **Also verified on Intel.** The same checks pass on an Intel Iris Xe host
  (ANV) with `patches/mesa/0021` ([zeus](docs/zeus.md)).
- **Also verified on NVIDIA.** On an RTX 5090 the same checks pass on
  `windows-11` and `windows-10` with vmlab 0.27, when the host config names a patched QEMU
  as the GPU's `qemu_binary` ([nvidia](docs/nvidia.md),
  [acceptance](docs/acceptance.md#nvidia-rtx-5090)), on both cards. The
  DirectX SDK D3D9 samples render there too after `patches/mesa/0022` to
  `0025` ([xid69](docs/xid69.md), [nvtex](docs/nvtex.md)). On a laptop RTX 4060 the GPU fell off the
  bus during a run ([zeus](docs/zeus.md)).
- Uninstall returns the guest to its state before the install
  ([evidence](docs/evidence/package/README.md)).

[docs/known-limits.md](docs/known-limits.md) lists everything else,
including the full host GPU matrix.

## Quick start

You need Docker without sudo, vmlab 0.27.0-alpha or later, the local
`windows-11` template, and an AMD, Intel or NVIDIA RTX 5090 GPU on the
host. NVIDIA also needs a patched QEMU as `qemu_binary` on its host config
block ([docs/nvidia.md](docs/nvidia.md)).

```sh
scripts/build.sh                               # writes out/package/
vmlab up                                       # in a lab dir like labs/probe/
scripts/install-into-vm.sh <lab-dir> <vm>      # install, with the reboots
```

The lab needs `secure_boot = false` and
`gpu { mode = "vulkan" host_gpu = "<name>" }`, where `<name>` is the
GPU as declared in the host config, `~/.config/vmlab/config.wcl`
(for example `gpu "AMD1" { address = "0000:72:00.0" }`), as in
`labs/probe/vmlab.wcl`. Without `host_gpu` the lab gets the host's default
GPU. [docs/install.md](docs/install.md) has the full
steps, including installing from inside the guest with `Install.cmd`.
`tests/e2e/run.sh --template windows-11` checks every Done item on a fresh
lab and destroys it afterwards.

## Install

A release has two assets, built by `scripts/release.sh`
([docs/release.md](docs/release.md)):

- `vmlab-win3d-<version>-windows-x64.zip`, the signed guest package. Unzip
  it in a Windows 10 or 11 guest with Secure Boot off and a Venus GPU, and
  run `Install.cmd` as Administrator. Its `README.txt` has the steps.
- `qemu-11.1.1-vmlab-egl-<version>-x86_64-linux.tar.zst`, the patched QEMU
  that NVIDIA hosts need. Extract it, run `install.sh`, and paste the
  `qemu_binary` line it prints into the host config's GPU block.

[docs/release.md](docs/release.md) covers both, and how to cut a release.
To build from source instead, see [Quick start](#quick-start).

## Documentation

| Page | Contents |
| --- | --- |
| [docs/index.md](docs/index.md) | Map of every document |
| [docs/install.md](docs/install.md) | Build, lab setup, install, what the installer changes |
| [docs/uninstall.md](docs/uninstall.md) | Removal back to the baseline |
| [docs/release.md](docs/release.md) | Cutting a release, and installing the release assets |
| [docs/stack.md](docs/stack.md) | Components, guest files, registration, pins, patches, licences |
| [docs/known-limits.md](docs/known-limits.md) | What does not work or is untested, and which host GPUs work |
| [docs/decisions/0001-signing.md](docs/decisions/0001-signing.md) | Why test signing with Secure Boot off |
| [docs/acceptance.md](docs/acceptance.md) | The Done criteria checked on Windows 11 and 10, and the evidence index |
| [docs/host.md](docs/host.md) | What the host and vmlab need for Venus |
| [docs/dxdiag.md](docs/dxdiag.md) | The DirectDraw HAL fix behind dxdiag's hardware acceleration |
| [docs/d3d11-apps.md](docs/d3d11-apps.md) | The DirectX SDK D3D11 samples: UMD fixes and a DXUT bug |
| [docs/nvidia.md](docs/nvidia.md) | Venus on the NVIDIA RTX 5090 |
| [docs/zeus.md](docs/zeus.md) | Venus on an Intel Iris Xe and a laptop RTX 4060 |

## Credits

This package is mostly other people's work, assembled, patched and tested.
The exact version of each project is pinned in a `PINS` file, and our changes
to it are in `patches/` (see [docs/stack.md](docs/stack.md)).

### Shipped in the guest package

| Project | What it provides here | Licence |
| --- | --- | --- |
| [Yttrium virtio-gpu driver](https://github.com/arehnman/yttrium-virtio-gpu) by Åke Rehnman | The WDDM kernel-mode driver (`viogpu3d.sys`) with Venus and blob support | BSD-3-Clause, MPL-2.0 |
| [virtio-win kvm-guest-drivers-windows](https://github.com/virtio-win/kvm-guest-drivers-windows), and its PR #943 "Virtio GPU 3D acceleration for windows" by max8rr8 | The VirtIO driver base that Yttrium's driver is forked from, and the 3D work this project started from | BSD-3-Clause |
| [Mesa](https://mesa3d.org/), through Yttrium's fork [virtio-win-mesa](https://github.com/arehnman/virtio-win-mesa) | The Venus Vulkan driver, the Direct3D 9/10/11 user-mode driver (Gallium d3d10umd with Yttrium's winsys), and OpenGL through WGL and Zink | MIT, MPL-2.0 (Yttrium files) |
| [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) and [dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv) | Direct3D 12 on Vulkan, and DXIL-to-SPIR-V shader translation | LGPL-2.1-or-later; MIT |
| [DXVK](https://github.com/doitsujin/dxvk) by Philip Rebohle | The DXGI implementation behind the Direct3D 12 bridge | zlib |
| Khronos [Vulkan-Loader](https://github.com/KhronosGroup/Vulkan-Loader), [Vulkan-Tools](https://github.com/KhronosGroup/Vulkan-Tools) and [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | `vulkan-1.dll` and `vulkaninfo.exe` | Apache-2.0 (Headers: Apache-2.0 or MIT) |

### Used to build and sign

| Project | Use |
| --- | --- |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) by Martin Storsjö | The clang/LLVM Windows cross toolchain for every component, the kernel driver included |
| [Microsoft DirectX-Headers](https://github.com/microsoft/DirectX-Headers), and the Windows SDK and WDK NuGet packages | Direct3D, DDI and kernel headers for cross builds |
| [Meson](https://mesonbuild.com/), CMake and Ninja | Build systems |
| [osslsigncode](https://github.com/mtrojnar/osslsigncode) and OpenSSL | Test-signing the driver on Linux |
| [generate-cat-file](https://github.com/LINBIT/generate-cat-file) by LINBIT | Creating the driver catalog (`.cat`) on Linux |

### Used on the host and for testing

| Project | Use |
| --- | --- |
| [QEMU](https://www.qemu.org/) and [virglrenderer](https://gitlab.freedesktop.org/virgl/virglrenderer) | The virtio-gpu device and the host side of Venus. Our egl-headless fix for NVIDIA is the branch [`vmlab/egl-headless-external-dmabuf`](https://github.com/VMLabDev/qemu/tree/vmlab/egl-headless-external-dmabuf) on VMLabDev/qemu, built at the commit `patches/host/PINS` pins (`78bc99a513`) and shipped as a release asset. |
| [vmlab](https://github.com/VMLabDev/vmlab) | The labs every test runs in |
| Microsoft DirectX SDK (June 2010) samples, [Blender](https://www.blender.org/), the LunarG Vulkan Runtime and the Visual C++ 2010 runtime | Unmodified third-party applications for the acceptance tests. They are downloaded at test time from their official sources and are not redistributed. |

## Licence

This repository's own scripts, build files, installer and documents are
under the MIT licence ([LICENSE](LICENSE)), copyright 2026 VMLabDev and
contributors.

That licence does not cover other people's code. Everything under
`patches/` modifies an upstream project and is under that project's
licence. Every component the scripts build or download, and so every file
in the guest package and the QEMU release asset, keeps its upstream
licence: BSD-3-Clause and MPL-2.0 (the KMD), MIT and MPL-2.0 (Mesa),
LGPL-2.1-or-later (vkd3d-proton), MIT (dxil-spirv), zlib (DXVK),
Apache-2.0 (the Vulkan loader) and GPL-2.0 (QEMU). See
[Credits](#credits) and [docs/stack.md](docs/stack.md).
