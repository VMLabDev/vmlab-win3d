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
- **NVIDIA partial.** On an RTX 5090 the probes pass, but only with a patched
  QEMU that vmlab cannot run yet ([nvidia](docs/nvidia.md)). On a laptop RTX
  4060 the GPU fell off the bus during a run ([zeus](docs/zeus.md)).
- Uninstall returns the guest to its state before the install
  ([evidence](docs/evidence/package/README.md)).

[docs/known-limits.md](docs/known-limits.md) lists everything else,
including the full host GPU matrix.

## Quick start

You need Docker without sudo, vmlab 0.25.0-alpha or later, the local
`windows-11` template, and an AMD or Intel GPU on the host.

```sh
scripts/build.sh                               # writes out/package/
vmlab up                                       # in a lab dir like labs/probe/
scripts/install-into-vm.sh <lab-dir> <vm>      # install, with the reboots
```

The lab needs `secure_boot = false` and
`gpu { mode = "vulkan" host_gpu = "<name>" }`, where `<name>` is the AMD
or Intel GPU as declared in the host config, `~/.config/vmlab/config.wcl`
(for example `gpu "AMD1" { address = "0000:72:00.0" }`), as in
`labs/probe/vmlab.wcl`. Without `host_gpu` the lab gets the host's default
GPU. [docs/install.md](docs/install.md) has the full
steps, including installing from inside the guest with `Install.cmd`.
`tests/e2e/run.sh --template windows-11` checks every Done item on a fresh
lab and destroys it afterwards.

## Documentation

| Page | Contents |
| --- | --- |
| [docs/index.md](docs/index.md) | Map of every document |
| [docs/install.md](docs/install.md) | Build, lab setup, install, what the installer changes |
| [docs/uninstall.md](docs/uninstall.md) | Removal back to the baseline |
| [docs/stack.md](docs/stack.md) | Components, guest files, registration, pins, patches, licences |
| [docs/known-limits.md](docs/known-limits.md) | What does not work or is untested, and which host GPUs work |
| [docs/decisions/0001-signing.md](docs/decisions/0001-signing.md) | Why test signing with Secure Boot off |
| [docs/acceptance.md](docs/acceptance.md) | The Done criteria checked on Windows 11 and 10, and the evidence index |
| [docs/host.md](docs/host.md) | What the host and vmlab need for Venus |
| [docs/dxdiag.md](docs/dxdiag.md) | The DirectDraw HAL fix behind dxdiag's hardware acceleration |
| [docs/d3d11-apps.md](docs/d3d11-apps.md) | The DirectX SDK D3D11 samples: UMD fixes and a DXUT bug |
| [docs/nvidia.md](docs/nvidia.md) | Venus on the NVIDIA RTX 5090 |
| [docs/zeus.md](docs/zeus.md) | Venus on an Intel Iris Xe and a laptop RTX 4060 |

## Licence

This repository's own scripts and documents have no licence statement yet.
The components it builds keep their upstream licences: BSD-3-Clause and
MPL-2.0 (the KMD), MIT and MPL-2.0 (Mesa), LGPL-2.1-or-later (vkd3d-proton),
MIT (dxil-spirv), zlib (DXVK) and Apache-2.0 (the Vulkan loader). See
[docs/stack.md](docs/stack.md).
