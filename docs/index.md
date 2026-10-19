# Documentation map

vmlab-win3d is a driver package for Windows 10 and 11 guests under vmlab. It
makes unmodified Direct3D 9 to 12, OpenGL and Vulkan applications render on
the host's GPU through virtio-gpu Venus.

**Status:** the Done criteria in [brief.md](brief.md) are met on an AMD host
(RADV), on fresh Windows 11 and Windows 10 labs
([acceptance](acceptance.md)). The same checks also pass on an Intel host
(ANV, [zeus](zeus.md)) and on both NVIDIA RTX 5090s with the patched QEMU
([nvidia](nvidia.md)). The laptop RTX 4060 on Zeus passes 7 of 12: its Venus
feedback writes never reach the guest ([known limits](known-limits.md#host-gpu-support)).

Start with [install](install.md) to build and use the package. Read
[stack](stack.md) to see what it installs, and [known limits](known-limits.md)
before you rely on it.

## Quick start

On a Linux host with Docker (no sudo), vmlab 0.25.0-alpha or later, the
local `windows-11` template, and an AMD or Intel GPU:

```sh
scripts/build.sh                               # writes out/package/
vmlab up                                       # in a lab dir like labs/probe/
scripts/install-into-vm.sh <lab-dir> <vm>      # install, with the reboots
```

The lab needs `secure_boot = false` and
`gpu { mode = "vulkan" host_gpu = "<name>" }`, where `<name>` is the AMD
or Intel GPU as declared in the host config, `~/.config/vmlab/config.wcl`
(for example `gpu "AMD1" { address = "0000:72:00.0" }`), as in
`labs/probe/vmlab.wcl`. To check every Done item on a fresh lab, run
`tests/e2e/run.sh --template windows-11`.

## Using the package

| Page | Read it to |
| --- | --- |
| [install.md](install.md) | Build the package, set up the lab, install it in a guest, and see every change the installer makes |
| [uninstall.md](uninstall.md) | Remove the package and return the guest to its state before the first install |
| [release.md](release.md) | Cut a release with `scripts/release.sh`, or install the release assets: the driver zip in a guest and the patched QEMU on an NVIDIA host |
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
| [d3d12app.md](d3d12app.md) | See how Microsoft's D3D12 samples are built unmodified with clang-cl and xwin, how they ran on AMD1 and Nvidia1, and why ExecuteIndirect draws nothing |
| [ei.md](ei.md) | See why D3D12 `ExecuteIndirect` with root-state arguments drew nothing on Venus, and the vkd3d-proton emulation `patches/vkd3d-proton/0006` |
| [nvidia.md](nvidia.md) | See why Venus failed on the RTX 5090 and what fixes it: `patches/mesa/0020` and a patched QEMU |
| [zeus.md](zeus.md) | See the package on an Intel Iris Xe (passes with `patches/mesa/0021`) and a laptop RTX 4060 (Venus feedback writes invisible to the guest) |
| [xid69.md](xid69.md) | See why D3D9 depth draws into the back buffer hung the RTX 5090 (Xid 69) and how `patches/mesa/0022` fixed it |
| [nvtex.md](nvtex.md) | See why mipmapped textures were black on NVIDIA, and the fixed-function and 16-bit format fixes `patches/mesa/0023` to `0025` |
| [ds.md](ds.md) | See why D3D11 domain shaders that index patch constants removed the device, and the fix `patches/mesa/0030` |
| [shaders2.md](shaders2.md) | See which indexed shader inputs and outputs work, how the repro assembles bytecode d3dcompiler_47 refuses, and the output-range and hull shader join-phase fixes `patches/mesa/0031` and `0032` |
| [installer.md](installer.md) | See how the package keeps its driver bound when Windows Update offers a newer Red Hat `viogpudo` (FeatureScore, forced install, device pin) |
| [wu.md](wu.md) | See why labs have a NAT NIC and run `slmgr /ato` (the evaluation templates shut down unactivated), and what a real Windows Update driver scan offers the device: nothing, and the package stays bound |
| [ddraw.md](ddraw.md) | See DirectDraw fullscreen exclusive mode, 16 bpp modes, and the hardware colour-key and mirror blits of `patches/mesa/0035` |
| [dd16.md](dd16.md) | See why 64-bit DirectDraw programs get no 16 bpp modes: the trace through `ddraw.dll`, win32k, CDD and `dxgkrnl.sys`, and the R5G6B5 KMD run |
| [present.md](present.md) | See why presents were slow (CPU reads of write-combined memory) and why Blender was translucent on Windows 10, and the fixes `patches/kmd/0002` and `patches/mesa/0026`, `0027` |
| [n5.md](n5.md) | See why D3D9 presents run below the refresh rate on NVIDIA hosts (slow wake-ups on virglrenderer's exportable fences) and the host-side virglrenderer fix |
| [ffp.md](ffp.md) | See how D3D9 fixed function was tested against the reference rasterizer, and the software-vertex-processing, fog and point fixes `patches/mesa/0040` to `0042` |
| [compat.md](compat.md) | See which unmodified free applications (Unigine Heaven and Valley, Godot, GZDoom, Crispy Doom) run on AMD1 and Nvidia2 under each API, and each failure in detail, including the Godot Vulkan Xid 69 |
| [ffp2.md](ffp2.md) | See D3D9 vertex blending, bump mapping, cube texgen, mixed devices and Windows 10 tested against the reference, and the bump-format and blend-palette changes `patches/mesa/0043` and `0044` |
| [unid3d.md](unid3d.md) | See the Unigine D3D9 and D3D11 findings F5 to F7: the D3D9 depth-bias fix for NVIDIA (`patches/mesa/0090`), Valley D3D9 running out of 32-bit address space, and Heaven D3D11's use-after-free |
| [unigl.md](unigl.md) | See the Unigine OpenGL finding F4 on NVIDIA: the RTX 5090's small BAR heap, the Venus allocation and Zink mapping fixes (`patches/mesa/0085`, `0086`, with nvva's `0070`), and the proof on both NVIDIA cards and AMD1 |

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
