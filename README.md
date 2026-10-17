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

Everything is built from pinned upstream sources by scripts in this
repository, in Docker on Linux. [docs/stack.md](docs/stack.md) follows a
frame through every layer.

## Status

Alpha, for lab VMs only. The package is test-signed and needs Secure Boot
off.

- Builds from a fresh clone with one command, reproducibly.
- On a fresh vmlab `windows-11` VM it installs, passes the D3D9, D3D11,
  D3D12, OpenGL and Vulkan probes on the GPU (x64 and x86), and uninstalls
  back to the original state ([evidence](docs/evidence/package/README.md)).
- The end-to-end acceptance run, including dxdiag and Windows 10, is being
  recorded.
- The host GPU must be AMD (RADV). Venus on NVIDIA does not work yet.

[docs/known-limits.md](docs/known-limits.md) lists everything else.

## Quick start

You need Docker without sudo, vmlab 0.24.0-alpha or later, the local
`windows-11` template, and an AMD GPU on the host.

```sh
scripts/build.sh                               # writes out/package/
vmlab up                                       # in a lab dir like labs/probe/
scripts/install-into-vm.sh <lab-dir> <vm>      # install, with the reboots
```

The lab needs `secure_boot = false` and
`gpu { mode = "vulkan" address = "<PCI address of the AMD GPU>" }`.
[docs/install.md](docs/install.md) has the full steps, including installing
from inside the guest with `Install.cmd`.

## Documentation

| Page | Contents |
| --- | --- |
| [docs/index.md](docs/index.md) | Map of every document |
| [docs/install.md](docs/install.md) | Build, lab setup, install, what the installer changes |
| [docs/uninstall.md](docs/uninstall.md) | Removal back to the baseline |
| [docs/stack.md](docs/stack.md) | Components, guest files, registration, pins, patches, licences |
| [docs/known-limits.md](docs/known-limits.md) | What does not work or is untested |
| [docs/decisions/0001-signing.md](docs/decisions/0001-signing.md) | Why test signing with Secure Boot off |
| [docs/host.md](docs/host.md) | What the host and vmlab need for Venus |

## Licence

This repository's own scripts and documents have no licence statement yet.
The components it builds keep their upstream licences: BSD-3-Clause and
MPL-2.0 (the KMD), MIT and MPL-2.0 (Mesa), LGPL-2.1-or-later (vkd3d-proton),
MIT (dxil-spirv), zlib (DXVK) and Apache-2.0 (the Vulkan loader). See
[docs/stack.md](docs/stack.md).
