# vmlab-win3d 0.1.1-alpha-rc3

Alpha, for test VMs only. Built from [`ee9ce49fefc7`](https://github.com/VMLabDev/vmlab-win3d/commit/ee9ce49fefc7d6820653b6014d44d876fc12965d)
(commit date 2026-10-19) by `scripts/release.sh 0.1.1-alpha-rc3`.

## Assets

| File | What | Bytes | sha256 |
| --- | --- | --- | --- |
| `vmlab-win3d-0.1.1-alpha-rc3-windows-x64.zip` | Guest driver package for Windows 10 and 11 x64 | 25541829 | `e39728c8d0ee24c81c91e86033b90349b3dc995d8a0ea55d806ba389df59917c` |
| `qemu-vmlab-egl-0.1.1-alpha-rc3-x86_64-linux.tar.zst` | Patched QEMU for NVIDIA hosts (x86_64 Linux) | 23562204 | `b244b68a565e038f14ab81dd0a77e3899dd4ba6bf5240e81139e00edbb0484d2` |
| `virglrenderer-vmlab-0.1.1-alpha-rc3-x86_64-linux.tar.zst` | Patched virglrenderer render server for NVIDIA hosts (x86_64 Linux) | 320936 | `3bb8a796be1837ac2c0c3e247ccd1360be2a3f8bfd295a308e6462bec53d3fd9` |

`SHA256SUMS` lists the same hashes. [docs/release.md](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/release.md)
explains how to install each asset.

- **Driver package.** Unzip in the guest, then run `Install.cmd` as
  Administrator from the `vmlab-win3d-*` folder; the zip's `README.txt` has
  the steps. Needs Secure Boot off and a Venus GPU (vmlab
  `gpu { mode = "vulkan" }`).
- **QEMU.** Only NVIDIA host GPUs need it. Extract the tarball and run
  `./install.sh`, then put the `qemu_binary` line it prints into the GPU's
  block in the vmlab host config (vmlab 0.26.0-alpha or later).
- **Render server.** Only NVIDIA host GPUs need it: it removes a 10 ms
  floor from every Venus fence there, which held windowed D3D9 to about
  40 frames per second. Extract the tarball and run `./install.sh`, then
  put the `qemu_env` line it prints into the same GPU block.

## Test-signing certificate

The kernel driver is test-signed. `Install.cmd` turns on Windows test
signing and trusts this certificate machine-wide (Trusted Root and Trusted
Publisher), so install the package only in throwaway test VMs.

| Field | Value |
| --- | --- |
| Subject | `O=vmlab-win3d local test (not for distribution),CN=vmlab-win3d Test Driver Signing` |
| SHA-1 thumbprint | `BEC7EE0705AFDB5E0BF72809F4E00F4D611F356A` |
| SHA-256 | `1646AE3F9C1C8C156F55A25504F258838B4413D3D97B0BEB3C3D17F54FDCA07F` |
| Valid | Oct 18 02:37:56 2026 GMT to Oct 15 02:37:56 2036 GMT |

## Host GPUs

From [docs/known-limits.md](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#host-gpu-support):

| Host GPU, driver | Host | Result | Needs | Evidence |
| --- | --- | --- | --- | --- |
| AMD Radeon iGPU (Granite Ridge), RADV, Mesa 26.2.2 | helios | **Full pass**: every Done check in `docs/brief.md` item 2, on `windows-11` and `windows-10`, and every stretch application renders, and the compatibility sweep passes 12 of 12 (package 0.1.1-alpha-rc2, vmlab 0.28.0-alpha) | A plain vmlab lab that names the AMD GPU (`host_gpu = "AMD1"`) | [`acceptance.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/acceptance.md) |
| NVIDIA RTX 5090, 610.57.04 | helios | **Full pass**, 12 of 12 Done checks, through `tests/e2e/run.sh` with package 0.1.1-alpha-rc1 and vmlab 0.28.0-alpha: `windows-11` and `windows-10` on `Nvidia1` and `windows-11` on `Nvidia2`; the guest lists exactly one Venus device, every stretch application renders, and the host logged no Xid. Depth draws into the back buffer need `patches/mesa/0022`, and mipmapped and R5G6B5 D3D9 textures need `0023` and `0025` (below). Applications that fill the 256 MiB BAR1 need `0070` and `0086`, and Vulkan applications with shading-rate state need `0075` (below) | `patches/mesa/0020`, `0022`, `0023` and `0025` (in the package) **and** the patched QEMU (the VMLabDev/qemu commit in `patches/host/PINS`) as the GPU's `qemu_binary` in the host config. Helios's host config gives both `Nvidia1` and `Nvidia2` one; without it the probes render but the console is black. The vmlab render server, selected with `qemu_env`, removes a 10 ms floor from every Venus fence ("Presentation pacing" below) | [`acceptance.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/acceptance.md#nvidia-rtx-5090), [`nvidia.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvidia.md) |
| Intel Iris Xe (Raptor Lake-P), ANV, Mesa 26.2.2 | Zeus | **Full pass** on `windows-11`, 12 of 12 Done checks. Without `patches/mesa/0021`, dwm crash-loops and the console is black | `patches/mesa/0021` (in the package) | [`zeus.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/zeus.md) |
| NVIDIA RTX 4060 Max-Q (laptop), 615.71.09 | Zeus | **Full pass**, 12 of 12 twice (2026-10-19, `bf4d948` of branch `work/nvfb`, package defaults, the patched QEMU, vmlab 0.28). On this host the guest does not see the GPU's writes to uncached host-visible memory (type 2) and the GPU does not see the guest's; without the fixes every Vulkan, D3D12 and OpenGL client hung on its first fence (Venus feedback lived there), and with only the feedback fix about one OpenGL readback in three came back black (Zink's upload buffers live there). In a fresh lab the first D3D12 probe could present only one frame in its 20 seconds (not caused by the fixes, cause not investigated); the probes now count `--seconds` from their first frame ([`x86d3d12.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/x86d3d12.md)) | `patches/mesa/0065` to `0067` (in the package) and the patched QEMU as `qemu_binary` | [`nvfb.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvfb.md), [`zeus.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/zeus.md#nvidia-rtx-4060) |

## Known limits in brief

Each item is a section of [docs/known-limits.md](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md).

- Host: [Two Windows VMs per RTX 5090 without resizable BAR](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#two-windows-vms-per-rtx-5090-without-resizable-bar)
- Host: [Requires vmlab 0.27.0-alpha or later](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#requires-vmlab-0270-alpha-or-later)
- Host: [Autologon values can vanish on a fresh clone](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#autologon-values-can-vanish-on-a-fresh-clone)
- Host: [Evaluation templates need the lab NIC and `slmgr /ato`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#evaluation-templates-need-the-lab-nic-and-slmgr-ato)
- Host: [QEMU crashes on shutdown](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#qemu-crashes-on-shutdown)
- Host: [The QEMU tarball suits current Arch-based hosts, and 0.1.0-alpha's needs AVX-512](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#the-qemu-tarball-suits-current-arch-based-hosts-and-010-alphas-needs-avx-512)
- Guest: [Test signing, Secure Boot off, and the Test Mode watermark](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#test-signing-secure-boot-off-and-the-test-mode-watermark)
- Guest: [`d3d12.dll` is replaced](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#d3d12dll-is-replaced)
- Guest: [No native D3D12 DDI](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#no-native-d3d12-ddi)
- Guest: [Fast Startup and hibernation are off](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#fast-startup-and-hibernation-are-off)
- Guest: [Presentation pacing is unmeasured; D3D9 on NVIDIA needs the vmlab render server](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#presentation-pacing-is-unmeasured-d3d9-on-nvidia-needs-the-vmlab-render-server)
- Guest: [Kernel driver diagnostics](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#kernel-driver-diagnostics)
- Guest: [Windows 10](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#windows-10)
- Guest: [Application compatibility is barely tested](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#application-compatibility-is-barely-tested)
- Guest: [OpenGL on NVIDIA: buffers stay out of the small BAR heap](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#opengl-on-nvidia-buffers-stay-out-of-the-small-bar-heap)
- Guest: [32-bit Unigine: Valley D3D9 address space (fixed); Heaven D3D11 use-after-free (most likely fixed)](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#32-bit-unigine-valley-d3d9-address-space-fixed-heaven-d3d11-use-after-free-most-likely-fixed)
- Guest: [D3D9 fixed function: what is and is not covered](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#d3d9-fixed-function-what-is-and-is-not-covered)
- Guest: [DXUT (June 2010) D3D11 samples place their window off the screen](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#dxut-june-2010-d3d11-samples-place-their-window-off-the-screen)
- Guest: [Indexed shader inputs and outputs: what is covered](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#indexed-shader-inputs-and-outputs-what-is-covered)
- Guest: [D3D12: ExecuteIndirect with vertex or index buffer arguments, and an NVIDIA first-boot dwm crash](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#d3d12-executeindirect-with-vertex-or-index-buffer-arguments-and-an-nvidia-first-boot-dwm-crash)
- Guest: [The first D3D12 launch in a new guest waits on Defender's cloud check](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#the-first-d3d12-launch-in-a-new-guest-waits-on-defenders-cloud-check)
- Guest: [DirectDraw: 16 bpp only for 32-bit programs, and flips below the refresh rate on NVIDIA](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#directdraw-16-bpp-only-for-32-bit-programs-and-flips-below-the-refresh-rate-on-nvidia)
- Guest: [Vulkan swapchains on NVIDIA cost one copy per present](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#vulkan-swapchains-on-nvidia-cost-one-copy-per-present)
- Guest: [Other limits](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/known-limits.md#other-limits)

## Upstream pins

Every upstream input, from the `PINS` files (`scripts/pins-table.py`), and
every patch this repository applies.

| Build | Input | Version or tag | Commit or digest | sha256 of the download | Source |
| --- | --- | --- | --- | --- | --- |
| [`d3d12`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/PINS) | vkd3d-proton | `v3.0.1` | `3b10bd7a7ec6a7347e616cf8bea59333afec2255` | — | https://github.com/HansKristian-Work/vkd3d-proton |
| [`d3d12`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/PINS) | dxil-spirv | `master` | `8434b0639fb2a48ce13aac08a420ecce5d23df0e` | — | https://github.com/HansKristian-Work/dxil-spirv |
| [`d3d12`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/PINS) | dxvk | `v3.1.1` | `b1a1c99ab52b687cf950d62c88bc2fa316b41663` | — | https://github.com/doitsujin/dxvk |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | samples | `master` | `e5975f9b0744fc5096dd593ed72bf8f5c004164f` | — | https://github.com/microsoft/DirectX-Graphics-Samples |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | xwin | `0.10.0` | — | `d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88` | https://github.com/Jake-Shadle/xwin/releases/download/0.10.0/xwin-0.10.0-x86_64-unknown-linux-musl.tar.gz |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | vsman | `17.14.37806.7` | — | `99dda859c4ae3c446eb25620d1a6237bb135d95d14d55cd1d30a15340bb39d80` | https://download.visualstudio.microsoft.com/download/pr/c95450be-0a45-4779-b142-33f567ccfd6e/a871a5ff41acebbe5037455fd6f51fcd4907b47d8f3c9860871ea0b222a2fd45/VisualStudio.vsman |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | crt-version | `14.44.17.14` | — | — | — |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | sdk-version | `10.0.26100` | — | — | — |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | winsysroot | — | — | `6fa919063f99d4a9631b1cd4af63bae2fec84d53732d0288d0b89b4523bb493f` | — |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | nuget-d3d12 | `1.618.3` | — | `799981b3569eae3a08468891fd4a0b027907b262a75a5cc3f48d621ee68a21db` | https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.618.3/microsoft.direct3d.d3d12.1.618.3.nupkg |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | nuget-pix | `1.0.240308001` | — | `726acc93d6968e2146261a1e415521747d50ad69894c2b42b5d0d4c29fd66ec4` | https://api.nuget.org/v3-flatcontainer/winpixeventruntime/1.0.240308001/winpixeventruntime.1.0.240308001.nupkg |
| [`d3d12-samples`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12-samples/PINS) | dxc | `v1.8.2505.1` | — | `f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c` | https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/linux_dxc_2025_07_14.x86_64.tar.gz |
| [`kmd`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/kmd/PINS) | yttrium-virtio-gpu | `yttrium-experimental-2026-09-27` | `b99d951a1c97f6fb3a1adbc08a4d2ab9ce9f5a15` | — | https://github.com/arehnman/yttrium-virtio-gpu |
| [`kmd`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/kmd/PINS) | llvm-mingw | `20261006` | — | `5f9c6ed95b2d4bdb2869a488c5fd5857fbdabcf288a0aa3eb1da43f6a08d8ab4` | https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64.tar.xz |
| [`kmd`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/kmd/PINS) | wdk-x64 | `10.0.26100.6584` | — | `c393d03dfb640b5c92f546b32f6770ef68cd3aaf691956e7d66d8e2c28a1b55e` | https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/10.0.26100.6584/microsoft.windows.wdk.x64.10.0.26100.6584.nupkg |
| [`kmd`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/kmd/PINS) | sdk-cpp | `10.0.26100.6584` | — | `5d31b38205bdd9ac761b4cb39fbbc6b7209b01c11194324afc674d7d119483a0` | https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp/10.0.26100.6584/microsoft.windows.sdk.cpp.10.0.26100.6584.nupkg |
| [`kmd`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/kmd/PINS) | debian | `debian:12` | `sha256:2c037a04925515fdd6ea85ea14a682d0e79931f5e9f5d07b6dbfc6ba12f9e858` | — | docker.io/library/debian |
| [`mesa`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/PINS) | mesa | — | `374c30222e0c08b04783c173c127abb3e465d028` | `7d8538c1d17ffe2a2abe733532d1dd1be9b43089ec79e9c2460730ea0694f72a` | https://github.com/arehnman/virtio-win-mesa/archive/374c30222e0c08b04783c173c127abb3e465d028.tar.gz |
| [`mesa`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/PINS) | windows-sdk-cpp | `10.0.26100.1` | — | `b4730a467a8f29145fc0136b2b3f626767e985f6aa6e32f1352953c38d6ff5d2` | https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp/10.0.26100.1/microsoft.windows.sdk.cpp.10.0.26100.1.nupkg |
| [`mesa`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/PINS) | windows-wdk-x64 | `10.0.26100.1` | — | `247b2919ae451f65ba5f1cd51c7c39730fb0fc383d607f3e8ab317fddc8a8239` | https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/10.0.26100.1/microsoft.windows.wdk.x64.10.0.26100.1.nupkg |
| [`mesa`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/PINS) | DirectX-Headers | `v1.619.1` | `9e393d6d8a3b30dcc6f2806ef604ec16a27b0d7e` | `6193774904c940eebb9b0c51b816b93dd776cfeb25a951f0f4a58f22387e5008` | https://github.com/microsoft/DirectX-Headers/archive/refs/tags/v1.619.1.tar.gz |
| [`probes`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/probes/PINS) | vulkan-headers | `vulkan-sdk-1.4.363.0` | `6802bb4733b63ed5efd3adb308a6c885ef180ea1` | `4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e` | https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz |
| [`signing`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/signing/PINS) | generate-cat-file | — | `65ebf4de79a42b0a1104fbef37b1c207ff13e1e9` | — | https://github.com/LINBIT/generate-cat-file |
| [`signing`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/signing/PINS) | debian | `debian:12` | `sha256:2c037a04925515fdd6ea85ea14a682d0e79931f5e9f5d07b6dbfc6ba12f9e858` | — | docker.io/library/debian |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | base-image | `ubuntu:24.04` | `sha256:ac3a3d13e90c660a1cb3e89ec695533399f7378dc1ae5fbdc28eb5781d561594` | — | docker.io/library/ubuntu |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | apt-snapshot | `20261010T000000Z` | — | — | https://snapshot.ubuntu.com/ubuntu/20261010T000000Z/ |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | llvm-mingw | `20261006` | — | `5f9c6ed95b2d4bdb2869a488c5fd5857fbdabcf288a0aa3eb1da43f6a08d8ab4` | https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64.tar.xz |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | meson | `1.9.1` | — | `f824ab770c041a202f532f69e114c971918ed2daff7ea56583d80642564598d0` | https://pypi.org/project/meson/1.9.1/ |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | mako | `1.3.10` | — | `baef24a52fc4fc514a0887ac600f9f1cff3d82c61d4d700a1fa84d597b88db59` | https://pypi.org/project/Mako/1.3.10/ |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | markupsafe | `3.0.3` | — | `d6dd0be5b5b189d31db7cda48b91d7e0a9795f31430b7f271219ab30f1d3ac9d` | https://pypi.org/project/MarkupSafe/3.0.3/ |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | packaging | `25.0` | — | `29572ef2b1f17581046b3a2227d5c611fb25ec70ca1ba8554b24b0e69331a484` | https://pypi.org/project/packaging/25.0/ |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | pyyaml | `6.0.3` | — | `ba1cc08a7ccde2d2ec775841541641e4548226580ab850948cbfda66a1befcdc` | https://pypi.org/project/PyYAML/6.0.3/ |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | bison (apt) | `2:3.8.2+dfsg-1ubuntu0.24.04.1` | — | — | — |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | cmake (apt) | `3.28.3-1build7` | — | — | — |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | flex (apt) | `2.6.4-8.2build1` | — | — | — |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | glslang-tools (apt) | `15.1.0-2~ubuntu0.24.04.2` | — | — | — |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | ninja-build (apt) | `1.11.1-2` | — | — | — |
| [`toolchain`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/toolchain/PINS) | python3 (apt) | `3.12.3-0ubuntu2.1` | — | — | — |
| [`vulkan-sdk`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/vulkan-sdk/PINS) | Vulkan-Headers | `vulkan-sdk-1.4.363.0` | `6802bb4733b63ed5efd3adb308a6c885ef180ea1` | `4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e` | https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz |
| [`vulkan-sdk`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/vulkan-sdk/PINS) | Vulkan-Loader | `vulkan-sdk-1.4.363.0` | `fa218055ff4948b159f31d43f800c808a13d51df` | `941eff558fc74f248745fb7df367640cf89a8457f216dae498a74ae14b6ba6f7` | https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz |
| [`vulkan-sdk`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/vulkan-sdk/PINS) | Vulkan-Tools | `vulkan-sdk-1.4.363.0` | `6fe2055cf2fa921d52a4c6a31528cfc279a6977f` | `6cbeb83294434e636a0c78760f1a6dfe3e50c1bab0cffa154603ce73e70508f1` | https://github.com/KhronosGroup/Vulkan-Tools/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz |
| [`host`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/PINS) | virglrenderer | `1.3.0` | — | `065bc56e89e6f631f96101cd62eba0748e48eb888b434edc86e89d05395e76f3` | https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/1.3.0/virglrenderer-1.3.0.tar.gz |
| [`host`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/PINS) | meson | `1.12.1` | — | `930bc7542cbd9f57009e182fd014eba48cf1a0180a7b9006d2d32d8f168d8b02` | https://files.pythonhosted.org/packages/57/99/36cd25f9598eef70db97577440a70a360af505f6f01c2338a18d1556079c/meson-1.12.1-py3-none-any.whl |
| [`host`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/PINS) | pyyaml | `6.0.3` | — | `d76623373421df22fb4cf8817020cbb7ef15c725b9d5e45f17e189bfc384190f` | https://files.pythonhosted.org/packages/05/8e/961c0007c59b8dd7729d542c61a4d537767a59645b82a0b521206e1e25c2/pyyaml-6.0.3.tar.gz |
| [`host`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/PINS) | qemu | `branch vmlab/egl-headless-external-dmabuf` | `d8a6048aae11c5e26168e173a7c1b1a4335d6a23` | — | https://github.com/VMLabDev/qemu |

| Upstream | Patches | Patch | What it does | Explained in |
| --- | --- | --- | --- | --- |
| dxil-spirv | 0 | — | None; see [`patches/dxil-spirv/`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/dxil-spirv) | — |
| dxvk | 1 | [`0001`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/dxvk/0001-dxgi-Keep-the-swap-chain-description-when-a-resize-fails.patch) | dxgi: Keep the swap-chain description when a resize fails. | [`build/d3d12/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/README.md) |
| host/virglrenderer | 2 | [`0001`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/virglrenderer/0001-vkr-log-image-creation.patch) | vkr: log failed image creation and memory allocation, with the create-info (diagnostic render server) | [`docs/nvidia.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvidia.md) |
| | | [`0002`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/virglrenderer/0002-vkr-do-not-create-queue-sync-fences-exportable.patch) | vkr: do not create queue-sync fences exportable | [`docs/n5.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/n5.md) |
| host/virglrenderer/diagnostic | 1 | [`0040`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/virglrenderer/diagnostic/0040-vkr-trace-venus-commands-and-sync-submit.patch) | vkr: record the Venus command stream and wait on every submission (diagnostics) | [`docs/godot.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/godot.md) |
| host/virglrenderer/workaround | 1 | [`0003`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/host/virglrenderer/workaround/0003-vkr-drop-unsupported-feedback-loop-usage.patch) | vkr: retry vkCreateImage and the image-format queries without feedback-loop usage (superseded by mesa/0020; built only with WITH_WORKAROUND=1) | [`docs/nvidia.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvidia.md) |
| kmd | 2 | [`0001`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/kmd/0001-kmd-r10-copy.patch) | Convert R10G10B10A2 sources to 8-bit RGBA or BGRA in the KMD's CPU present blit (the prior art's r10 KMD) | [`build/kmd/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/kmd/README.md) |
| | | [`0002`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/kmd/0002-kmd-cache-gdi-shadow-and-staging-surfaces.patch) | viogpu3d: map GDI shadow and staging surfaces cached | [`docs/present.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/present.md) |
| mesa | 46 | [`0001`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0001-build-winddk-includes-and-x86-atomics.patch) | build: add the WDK include path and drop x86-unsafe 64-bit Interlocked calls | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0002`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0002-util-xmlconfig-case-insensitive-exe-match.patch) | util/xmlconfig: match drirc executable names case-insensitively on Windows | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0003`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0003-util-u_queue-windows-loader-lock-join.patch) | util/u_queue: do not join worker threads under the loader lock on Windows | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0004`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0004-wgl-unbind-through-stw-on-delete.patch) | wgl: unbind the current context through stw_make_current on delete | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0005`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0005-yttrium-blend-max-rt.patch) | d3d10umd, yttrium: honour blend max_rt and ignore factors when blending is off | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0006`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0006-d3d9-rendering-fixes.patch) | d3d10umd/d3d9, yttrium: D3D9 sampler, depth-bias, format and D32F fixes | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0007`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0007-d3d11-wddm20-ddi.patch) | d3d10umd: negotiate the WDDM 2.0 D3D11 DDI and report typed UAV loads | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0008`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0008-shader-translation-fixes.patch) | tgsi_to_nir, d3d10umd: fix array gradients and UAV load component selection | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0009`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0009-yttrium-compute-and-coherence.patch) | d3d10umd, yttrium: compute, constant-buffer and resource-coherence fixes | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0010`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0010-dxgi-blt-scaled-srgb-safe.patch) | d3d10umd: make the DXGI Blt scale and keep sRGB data bit-exact | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0011`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0011-venus-win32-nt-memory.patch) | venus: opaque Win32 (NT handle) memory export and import on D3DKMT | [`build/mesa/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/mesa/README.md) |
| | | [`0012`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0012-d3d9-directdraw-hal-caps.patch) | d3d10umd: report the D3D7 and D3D8 caps DirectDraw asks the D3D9 UMD for | [`docs/dxdiag.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/dxdiag.md) |
| | | [`0013`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0013-d3d9-write-only-lock-keeps-contents.patch) | d3d10umd: keep a surface's contents across a write-only D3D9 lock | [`docs/dxdiag.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/dxdiag.md) |
| | | [`0014`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0014-d3d9-ddraw-surfaces-are-present-sources.patch) | d3d10umd: make DirectDraw surfaces display targets | [`docs/dxdiag.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/dxdiag.md) |
| | | [`0015`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0015-d3d10umd-hull-shader-vprim-and-indexed-patch-io.patch) | d3d10umd: hull shader vPrim, indexed patch constants and control points | [`docs/d3d11-apps.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/d3d11-apps.md) |
| | | [`0016`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0016-d3d10umd-immediate-constant-buffer-relative-offset.patch) | d3d10umd: keep the offset in dynamically indexed immediate constant buffer reads | [`docs/d3d11-apps.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/d3d11-apps.md) |
| | | [`0017`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0017-yttrium-honour-wireframe-fill-mode.patch) | yttrium: honour D3D11_FILL_WIREFRAME | [`docs/d3d11-apps.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/d3d11-apps.md) |
| | | [`0020`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0020-yttrium-gate-feedback-loop-usage-on-format-support.patch) | yttrium: gate feedback-loop image usage on the renderer's format support | [`docs/nvidia.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvidia.md) |
| | | [`0021`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0021-yttrium-d3d11-typed-uav-loads-without-formatless-read.patch) | d3d10umd, yttrium: create WDDM 2.0 D3D11 devices without formatless storage-image reads; R32 typed UAV loads only, and TypedUAVLoadAdditionalFormats from the host (Intel ANV) | [`docs/zeus.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/zeus.md) |
| | | [`0022`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0022-yttrium-render-shadow-for-linear-colour-with-depth.patch) | yttrium: render LINEAR colour targets next to depth through an OPTIMAL shadow | [`docs/xid69.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/xid69.md) |
| | | [`0023`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0023-yttrium-shared-colour-optimal-when-linear-refused.patch) | yttrium: shared colour images fall back to OPTIMAL where the host refuses LINEAR | [`docs/nvtex.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvtex.md) |
| | | [`0024`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0024-d3d9-positiont-vs-for-runtime-fixed-function-ps.patch) | d3d9: generate the POSITIONT vertex shader for any runtime fixed-function pixel shader | [`docs/nvtex.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvtex.md) |
| | | [`0025`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0025-yttrium-packed-16-bit-formats-match-vulkan.patch) | yttrium: map D3D R5G6B5 and A1R5G5B5 to the matching Vulkan PACK16 formats | [`docs/nvtex.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvtex.md) |
| | | [`0026`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0026-d3d9-ddraw-present-sources-in-host-cached-memory.patch) | d3d9: put DirectDraw present sources in host-cached memory | [`docs/present.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/present.md) |
| | | [`0027`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0027-wgl-present-windows-opaque.patch) | wgl: present windows opaque through kopper | [`docs/present.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/present.md) |
| | | [`0030`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0030-d3d10umd-domain-shader-indexed-patch-constants.patch) | d3d10umd: domain shaders read patch constants with a register index | [`docs/ds.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ds.md) |
| | | [`0031`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0031-d3d10umd-output-index-ranges-in-every-stage.patch) | d3d10umd: output index ranges in every shader stage | [`docs/shaders2.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/shaders2.md) |
| | | [`0032`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0032-d3d10umd-hull-shader-join-phases-read-fork-phase-outputs.patch) | d3d10umd, yttrium: hull shader join phases read fork phase outputs | [`docs/shaders2.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/shaders2.md) |
| | | [`0035`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0035-d3d9-hardware-colour-key-and-mirror-blits.patch) | d3d9: hardware source colour-key and mirror blits for DirectDraw | [`docs/ddraw.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ddraw.md) |
| | | [`0040`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0040-d3d9-swvp-processed-vertices-keep-declaration-vs.patch) | d3d9: keep the declaration-built vertex shader for software-processed vertices | [`docs/ffp.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ffp.md) |
| | | [`0041`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0041-d3d9-fixed-function-fog.patch) | d3d9: fixed-function fog for perspective table fog, vertex fog clamping and pre-transformed vertices | [`docs/ffp.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ffp.md) |
| | | [`0042`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0042-d3d9-fixed-function-points.patch) | d3d9: point expansion keeps vertex attributes, perspective size and per-vertex PSIZE | [`docs/ffp.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ffp.md) |
| | | [`0043`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0043-d3d9-bump-luminance-formats.patch) | d3d9: L6V5U5 and X8L8V8U8 bump maps for BUMPENVMAPLUMINANCE | [`docs/ffp2.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ffp2.md) |
| | | [`0044`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0044-d3d9-indexed-vertex-blend-palette.patch) | d3d9: report the runtime's matrix palette for indexed vertex blending | [`docs/ffp2.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ffp2.md) |
| | | [`0060`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0060-d3d10umd-trim-residency-set-leaves-the-context-alone.patch) | d3d10umd: TrimResidencySet leaves the pipe context alone | [`docs/dwmheap.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/dwmheap.md) |
| | | [`0065`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0065-venus-feedback-buffers-in-host-cached-memory.patch) | venus: feedback buffers in host-cached memory | [`docs/nvfb.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvfb.md) |
| | | [`0066`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0066-venus-disable-feedback-when-writes-do-not-arrive.patch) | venus: disable feedback for a device whose feedback writes do not arrive | [`docs/nvfb.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvfb.md) |
| | | [`0067`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0067-venus-back-uncached-host-memory-with-cached-when-gpu-writes-do-not-arrive.patch) | venus: back uncached host memory with cached memory when GPU writes do not arrive | [`docs/nvfb.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvfb.md) |
| | | [`0070`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0070-venus-hide-host-access-to-small-bar-heap.patch) | venus: hide host access to a small BAR heap | [`docs/nvva.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/nvva.md) |
| | | [`0075`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0075-wsi-win32-blit-shared-swapchain-when-linear-cannot-render.patch) | wsi/win32: render DXGI_SHARED swapchains through an OPTIMAL image when LINEAR cannot be a render target | [`docs/godot.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/godot.md) |
| | | [`0080`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0080-wgl-pin-opengl32-so-pixel-formats-set-through-gdi-last.patch) | wgl: pin opengl32.dll so pixel formats set through GDI last | [`docs/gzdoom.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/gzdoom.md) |
| | | [`0085`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0085-venus-allocate-from-small-device-local-heap-synchronously.patch) | venus: allocate from a small device-local heap synchronously | [`docs/unigl.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/unigl.md) |
| | | [`0086`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0086-zink-keep-buffers-mappable-without-visible-vram.patch) | zink: keep buffers mappable when CPU-visible VRAM is missing or full | [`docs/unigl.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/unigl.md) |
| | | [`0090`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0090-yttrium-d3d9-depth-bias-without-float-representation.patch) | yttrium: convert D3D9 depth bias when the float representation is missing | [`docs/unid3d.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/unid3d.md) |
| | | [`0091`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0091-d3d9-stop-copying-vertex-buffer-locks-for-ordered-worker.patch) | d3d9: stop copying every vertex-buffer lock for the ordered worker | [`docs/f6va.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/f6va.md) |
| | | [`0092`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/mesa/0092-yttrium-bound-in-flight-uploads.patch) | yttrium: bound in-flight upload buffers in 32-bit processes | [`docs/f6va.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/f6va.md) |
| vkd3d-proton | 6 | [`0001`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/vkd3d-proton/0001-vkd3d-Reject-root-signatures-with-overlapping-regist.patch) | vkd3d: Reject root signatures with overlapping register bindings. | [`build/d3d12/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/README.md) |
| | | [`0002`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/vkd3d-proton/0002-vkd3d-Report-FORMAT_INFO-plane-counts-without-a-Vulk.patch) | vkd3d: Report FORMAT_INFO plane counts without a Vulkan format mapping. | [`build/d3d12/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/README.md) |
| | | [`0003`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/vkd3d-proton/0003-vkd3d-Allow-depth-stencil-textures-in-CPU-visible-he.patch) | vkd3d: Allow depth-stencil textures in CPU-visible heaps. | [`build/d3d12/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/README.md) |
| | | [`0004`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/vkd3d-proton/0004-vkd3d-Default-the-Windows-shader-cache-to-LocalAppDa.patch) | vkd3d: Default the Windows shader cache to LocalAppData. | [`build/d3d12/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/README.md) |
| | | [`0005`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/vkd3d-proton/0005-d3d12-Build-a-system-wide-d3d12.dll-bridge.patch) | d3d12: Build a system-wide d3d12.dll bridge. | [`build/d3d12/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/d3d12/README.md) |
| | | [`0006`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/vkd3d-proton/0006-vkd3d-Emulate-state-changing-ExecuteIndirect-without.patch) | vkd3d: Emulate state-changing ExecuteIndirect without EXT_dgc. | [`docs/ei.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/docs/ei.md) |
| vulkan-tools | 1 | [`0001`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/patches/vulkan-tools/0001-vulkaninfo-Load-user32-on-every-Windows-compiler.patch) | vulkaninfo: Load user32 on every Windows compiler | [`build/vulkan-sdk/README.md`](https://github.com/VMLabDev/vmlab-win3d/blob/ee9ce49fefc7d6820653b6014d44d876fc12965d/build/vulkan-sdk/README.md) |

## QEMU build

```
qemu:           QEMU emulator version 11.1.50 (vmlab d8a6048aae11)
source:         https://github.com/VMLabDev/qemu branch vmlab/egl-headless-external-dmabuf
commit:         d8a6048aae11c5e26168e173a7c1b1a4335d6a23
base:           a19f59a72d (upstream)
series:         d8a6048aae ui/sdl2: show external-only dma-buf scanouts
series:         f95d311446 ui/gtk: show external-only dma-buf scanouts
series:         40d3c41a0a ui/egl-headless: show external-only dma-buf scanouts
series:         dd46c88676 ui/egl-helpers: let an egl_fb wrap an external texture
series:         674931d277 ui/shader: add a texture blit program for external textures
series:         acb3659eea ui/egl-helpers: import external-only dma-bufs as external textures
build host:     CachyOS, x86_64, built 2026-10-19
compiler:       cc (GCC) 16.2.1 20260810
compiler flags: -march=x86-64-v2 -mtune=generic (link: -z x86-64-v2)
needs ISA:      x86-64-baseline, x86-64-v2
glibc:          2.44
needs GLIBC:    GLIBC_2.43
lib glib-2.0:   2.88.3
lib pixman-1:   0.46.4
lib epoxy:      1.5.10
lib gbm:        26.2.2-arch3.2
lib virglrenderer: 1.3.0
lib libdrm:     2.4.134
lib slirp:      4.9.4
lib zlib:       1.3.1.zlib-ng
lib libzstd:    1.5.7
lib libpng:     1.6.58
lib libjpeg:    3.2.0
lib gnutls:     3.8.13
lib libudev:    261
NEEDED:         libcapstone.so.5 libgnutls.so.30 libpng16.so.16 libpixman-1.so.0 libz.so.1 libjpeg.so.8 libsasl2.so.3 libepoxy.so.0 libsnappy.so.1 liblzo2.so.2 libnuma.so.1 libkeyutils.so.1 libgio-2.0.so.0 libgobject-2.0.so.0 libglib-2.0.so.0 librdmacm.so.1 libibverbs.so.1 libzstd.so.1 libslirp.so.0 libvdeplug.so.3 libxdp.so.1 libbpf.so.1 libdw.so.1 libcbor.so.0.14 libudev.so.1 libusb-1.0.so.0 libdaxctl.so.1 libseccomp.so.2 libfdt.so.1 libncursesw.so.6 libgmodule-2.0.so.0 libgbm.so.1 libbrlapi.so.0.8 libvirglrenderer.so.1 librutabaga_gfx_ffi.so libcacard.so.0 libusbredirparser.so.1 libpam.so.0 libfuse3.so.4 libiscsi.so.11 libaio.so.1 liburing.so.2 libcurl.so.4 libnfs.so.16.2.0 libssh.so.4 libbz2.so.1.0 libasound.so.2 libpulse.so.0 libjack.so.0 libsndio.so.7 libpipewire-0.3.so.0 libm.so.6 libc.so.6
```

## Render server build

```
component:      virgl_render_server (vmlab render server)
virglrenderer:  1.3.0 https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/1.3.0/virglrenderer-1.3.0.tar.gz
sha256:         065bc56e89e6f631f96101cd62eba0748e48eb888b434edc86e89d05395e76f3
patch:          0001-vkr-log-image-creation.patch 1391a6b58249e2b7
patch:          0002-vkr-do-not-create-queue-sync-fences-exportable.patch 3b6aa0716822559b
build host:     CachyOS, x86_64, built 2026-10-19
compiler:       cc (GCC) 16.2.1 20260810
compiler flags: -march=x86-64-v2 -mtune=generic (link: -z x86-64-v2)
needs ISA:      x86-64-v2
meson:          1.12.1, PyYAML 6.0.3
glibc:          2.44
needs GLIBC:    GLIBC_2.38
lib libdrm: 2.4.134
lib gbm: 26.2.2-arch3.2
lib vulkan: 1.4.357
host libvirglrenderer: 1.3.0 (must be 1.3.0: same render-server protocol)
```
