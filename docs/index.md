# Documentation map

Start with [install](install.md) to build and use the package. Read
[stack](stack.md) to see what it installs and [known limits](known-limits.md)
before you rely on it.

## Using the package

| Page | Read it to |
| --- | --- |
| [install.md](install.md) | Build the package, set up the lab, install it in a guest, and see every change the installer makes |
| [uninstall.md](uninstall.md) | Remove the package and return the guest to its state before the first install |
| [known-limits.md](known-limits.md) | Find out what does not work, has not been tested, or needs a workaround, with evidence |
| [`installer/README.txt`](../installer/README.txt) | The short guide that ships inside the bundle |

## How it works

| Page | Read it to |
| --- | --- |
| [stack.md](stack.md) | Follow a frame from the application to the host GPU: every component, its guest files and registration, upstream pin, patches and licence |
| [decisions/0001-signing.md](decisions/0001-signing.md) | Understand why the KMD is test-signed with Secure Boot off, and what the installer does about it |
| [host.md](host.md) | See what this host and vmlab need for Venus to work, measured setting by setting |
| [plan.md](plan.md) | See the target stack, the build layout contract, the work slices and the open decisions |
| [brief.md](brief.md) | Read the project's goal, constraints and definition of done |
| [prior-art.md](prior-art.md) | Find what the earlier attempt in `/home/wil/dev/virtio` proved, what it got wrong, and what was reused |

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

`scripts/build.sh` builds everything; `scripts/pins-table.py` regenerates the
pin and patch tables in `stack.md`.

## Evidence

| Page | Shows |
| --- | --- |
| [`evidence/package/README.md`](evidence/package/README.md) | A fresh-clone build, then install, probes, rerun, uninstall and reinstall on a fresh `windows-11` lab |
| [`evidence/kmd/README.md`](evidence/kmd/README.md) | The KMD's test signature accepted and the driver loaded in a guest |
| [`evidence/probes/README.md`](evidence/probes/README.md) | The probes and guest scripts on Microsoft's software renderers, before any driver from this repository |
| [`evidence/host/`](evidence/host/) | The host-probe captures that `host.md` cites |
| `acceptance.md` | Being written: the Done criteria in `brief.md` checked one by one on fresh Windows 11 and Windows 10 labs |
