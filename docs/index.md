# Documentation map

vmlab-win3d is a driver package for Windows 10 and 11 guests under vmlab. It
makes unmodified Direct3D 9 to 12, OpenGL and Vulkan applications render on
the host's GPU through virtio-gpu Venus.

**Status:** the Done criteria in [brief.md](brief.md) are met on an AMD host
(RADV), on fresh Windows 11 and Windows 10 labs
([acceptance](acceptance.md)). The same checks also pass on an Intel host
(ANV, [zeus](zeus.md)). NVIDIA works only in part
([known limits](known-limits.md#host-gpu-support)).

Start with [install](install.md) to build and use the package. Read
[stack](stack.md) to see what it installs, and [known limits](known-limits.md)
before you rely on it.

## Quick start

On a Linux host with Docker (no sudo), vmlab 0.24.0-alpha or later, the
local `windows-11` template, and an AMD or Intel GPU:

```sh
scripts/build.sh                               # writes out/package/
vmlab up                                       # in a lab dir like labs/probe/
scripts/install-into-vm.sh <lab-dir> <vm>      # install, with the reboots
```

The lab needs `secure_boot = false` and
`gpu { mode = "vulkan" address = "<PCI address of the GPU>" }`
(`labs/probe/vmlab.wcl`). To check every Done item on a fresh lab, run
`tests/e2e/run.sh --template windows-11`.

## Using the package

| Page | Read it to |
| --- | --- |
| [install.md](install.md) | Build the package, set up the lab, install it in a guest, and see every change the installer makes |
| [uninstall.md](uninstall.md) | Remove the package and return the guest to its state before the first install |
| [known-limits.md](known-limits.md) | Find out what does not work, has not been tested, or needs a workaround, including which host GPUs work |
| [`installer/README.txt`](../installer/README.txt) | The short guide that ships inside the bundle |

## How it works

| Page | Read it to |
| --- | --- |
| [stack.md](stack.md) | Follow a frame from the application to the host GPU: every component, its guest files and registration, upstream pin, patches and licence |
| [decisions/0001-signing.md](decisions/0001-signing.md) | Understand why the KMD is test-signed with Secure Boot off, and what the installer does about it |
| [host.md](host.md) | See what the host and vmlab need for Venus to work, measured setting by setting |
| [plan.md](plan.md) | See the target stack, the build layout contract, the work slices and the open decisions |
| [brief.md](brief.md) | Read the project's goal, constraints and definition of done |
| [prior-art.md](prior-art.md) | Find what the earlier attempt in `/home/wil/dev/virtio` proved, what it got wrong, and what was reused |

## Results and investigations

| Page | Read it to |
| --- | --- |
| [acceptance.md](acceptance.md) | See each Done item checked on fresh `windows-11` and `windows-10` labs, the stretch applications, and the index of every evidence directory |
| [dxdiag.md](dxdiag.md) | See why dxdiag reported DirectDraw "Not Available" and how `patches/mesa/0012` to `0014` fixed it |
| [d3d11-apps.md](d3d11-apps.md) | See why two DirectX SDK D3D11 samples did not show, the UMD fixes `patches/mesa/0015` to `0017`, and the DXUT window bug |
| [nvidia.md](nvidia.md) | See why Venus failed on the RTX 5090 and what fixes it: `patches/mesa/0020` and a patched QEMU |
| [zeus.md](zeus.md) | See the package on an Intel Iris Xe (passes with `patches/mesa/0021`) and a laptop RTX 4060 (failed) |

## Building

| Page | Covers |
| --- | --- |
| [`build/toolchain/README.md`](../build/toolchain/README.md) | The shared Docker image with llvm-mingw, Meson and CMake |
| [`build/mesa/README.md`](../build/mesa/README.md) | The Yttrium D3D UMD, the WGL/Zink ICD and the Venus Vulkan ICD, and the Mesa patch series |
| [`build/vulkan-sdk/README.md`](../build/vulkan-sdk/README.md) | The Khronos Vulkan loader and `vulkaninfo.exe` |
| [`build/d3d12/README.md`](../build/d3d12/README.md) | vkd3d-proton, DXVK's DXGI and the `d3d12.dll` bridge |
| [`build/kmd/README.md`](../build/kmd/README.md) | The viogpu3d KMD, its helpers and the INF, built on Linux |
| [`probes/README.md`](../probes/README.md) | The D3D9, D3D11, D3D12, OpenGL and Vulkan test programs |
| [`tests/guest/README.md`](../tests/guest/README.md) | The guest scripts that run the probes, dxdiag and the GPU state dump |
| [`patches/dxil-spirv/README.md`](../patches/dxil-spirv/README.md) | Why dxil-spirv has no patches |

`scripts/build.sh` builds everything. `scripts/pins-table.py --write`
regenerates the pin and patch tables in `stack.md`, and `--check` fails when
they are stale. `scripts/check-docs.py` checks that every repository path
named in the documents exists. `patches/host/` holds host-side QEMU and
virglrenderer patches; they are not part of the guest package
([stack](stack.md#host-patches)).

## Evidence

Every directory under [`evidence/`](evidence/) is listed, with what it proves
and the vmlab version and commit behind it, in
[acceptance.md, "Evidence index"](acceptance.md#evidence-index).
