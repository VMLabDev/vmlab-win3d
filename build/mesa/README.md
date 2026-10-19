# mesa

Builds the Yttrium Mesa drivers for x86_64 and i686 Windows from
`arehnman/virtio-win-mesa` at `374c30222e0c08b04783c173c127abb3e465d028`
(branch `yttrium-experimental`, Mesa 26.1.0) plus the patch series in
`patches/mesa/`. The build uses the shared llvm-mingw toolchain image from
`build/toolchain/`.

## Run

```sh
build/mesa/build.sh
```

The script builds or reuses the toolchain image, then runs `inner.sh` in a
container as your uid. Network access is used only for the pinned downloads in
`PINS`, and each run checks their sha256, also when they are cached. Each run
unpacks a fresh source tree, applies the patches, and deletes the build
trees, so a second run starts clean. `MESA_JOBS=<n>` limits Ninja's
parallelism; the default is `nproc`.

## Outputs

```
out/mesa/x64/   viogpu_d3d10.dll  viogpu_wgl.dll  libvulkan_virtio.dll  virtio_icd.json
out/mesa/x86/   (the same four files, 32-bit)
```

| File | What it is | Exports checked |
| --- | --- | --- |
| `viogpu_d3d10.dll` | Yttrium Gallium UMD for D3D9, D3D10 and D3D11, registered through the KMD's `UserModeDriverName[Wow]` | `OpenAdapter` (D3D9), `OpenAdapter10`, `OpenAdapter10_2` |
| `viogpu_wgl.dll` | Mesa WGL ICD with Zink on the Venus ICD, registered as `OpenGLDriverName[Wow]` | `DrvValidateVersion`, `DrvSetCallbackProcs`, `DrvCreateContext`, `DrvSetContext`, `DrvSwapBuffers`, `DrvGetProcAddress`, `DrvDescribePixelFormat`, `DrvSetPixelFormat` and the other `Drv*` WGL ICD entry points |
| `libvulkan_virtio.dll` | Venus Vulkan ICD on the Yttrium KMD (D3DKMT escapes) | `vk_icdGetInstanceProcAddr`, `vk_icdNegotiateLoaderICDInterfaceVersion`, `vk_icdGetPhysicalDeviceProcAddr` |
| `virtio_icd.json` | ICD manifest for the DLL next to it: `library_path` `.\libvulkan_virtio.dll`, `library_arch` 64 or 32, `api_version` as Mesa generates it (1.4.348) | — |

Meson's own `virtio_icd.<arch>.json` assumes a Unix install tree
(`../../../bin/vulkan_virtio.dll`), so the build writes its own manifest. The
Vulkan loader resolves a relative `library_path` against the manifest's
directory, so the manifest and the DLL must be installed in the same directory.

The DLLs link libc++, libunwind and winpthread statically (`-static`). They
import only GDI32, KERNEL32, USER32, `api-ms-win-core-synch-l1-2-0` and the
UCRT `api-ms-win-crt-*` API sets. They are stripped (`--strip-unneeded`), and
the PE timestamp is zero (`--no-insert-timestamp`).

The Meson options are the prior art's (`R/build.sh`, prior-art section 6):
`-Dplatforms=windows -Dgallium-drivers=zink -Dgallium-yttrium=true
-Dgallium-d3d10umd=true -Dgallium-wgl-dll-name=viogpu_wgl
-Dgallium-d3d10-dll-name=viogpu_d3d10 -Dvulkan-drivers=virtio -Dllvm=disabled
-Dzlib=disabled -Dzstd=disabled -Dexpat=disabled -Dbuild-tests=false`.

## Inputs

`PINS` lists every download with its sha256:

- the Mesa source tarball at the commit above;
- `microsoft.windows.sdk.cpp` and `microsoft.windows.wdk.x64` 10.0.26100.1
  from nuget.org. `prepare-winddk.py` extracts the 13 DDK headers the
  D3D10 UMD and Venus need into `include/winddk`. These are the same packages
  the prior art used (identical sha256);
- DirectX-Headers v1.619.1, unpacked as Mesa's `subprojects/DirectX-Headers-1.0`.
  It is the revision of Mesa's own wrap file. Meson runs with
  `--wrap-mode=nodownload`, so it never fetches anything.

## Patches

The series has two parts, and the patches apply in name order:

- **0001 to 0011** come from the prior art. The codex session left its fixes
  uncommitted in `R/mesa`, and its exported `R/mesa-driver.patch` is stale
  (prior-art section 3). These patches were re-exported from
  `git diff 374c3022` of that working tree. It had 43 changed files,
  +1793/−161, and two untracked files. The two untracked files are the
  mixed-case header symlinks that `prepare-build.py` creates, so they are
  build output, not source. The diff was then split by topic. Each patch has
  a header that says what it fixes and which prior-art finding it comes from.
- **0012 to 0017, 0020 to 0027, 0030 to 0032, 0035, 0040 to 0044, 0060, 0070, 0085 to 0086 and 0090** are this repository's own fixes, found by

0005 to 0010, every patch from 0012 to 0026 and 0030 to 0032 change
`viogpu_d3d10.dll`, the D3D9/D3D10/D3D11 UMD (the `d3d10umd` frontend and the
Yttrium winsys). 0032 also changes Gallium's `tgsi_to_nir`, in a branch that
only a TGSI tessellation control shader reaches, which only the UMD makes.
0001 to 0003 are build and utility fixes that every DLL shares, 0004 and 0027
are in WGL, 0011, 0070 and 0085 are in the Venus ICD, and 0086 is in Zink (WGL).

### From the prior art

| Patch | Fixes | Prior art |
| --- | --- | --- |
| 0001 build-winddk-includes-and-x86-atomics | WDK include path for libgallium and Venus. Replaces `InterlockedIncrement64`, which i686 lacks | F1 |
| 0002 util-xmlconfig-case-insensitive-exe-match | drirc `executable=` matching ignores case on Windows | F2 |
| 0003 util-u_queue-windows-loader-lock-join | GL shutdown deadlock: queue join under the loader lock | F215–F219 (v15) |
| 0004 wgl-unbind-through-stw-on-delete | Framebuffer leak when deleting the current GL context | F220–F221 (v16) |
| 0005 yttrium-blend-max-rt | Garbage blend factors past `max_rt` crashed Heaven | F3 |
| 0006 d3d9-rendering-fixes | D3D9 sampler type, depth bias, A4R4G4B4, A8L8, D32F, depth uploads | F4, F15, F16, F20 |
| 0007 d3d11-wddm20-ddi | WDDM 2.0 D3D11 DDI, GPU VA caps, typed UAV loads | F11–F14 |
| 0008 shader-translation-fixes | Array TXD gradients; raw and typed UAV load swizzles | F17, F28, F29 |
| 0009 yttrium-compute-and-coherence | Cube/array views, resource waits, staged uploads, compute buffer SRVs, GPU-written constants, native indirect dispatch | F21, F22, F33, F38, F224–F231, staged-draw-0212 |
| 0010 dxgi-blt-scaled-srgb-safe | DXGI present Blt scaling with sRGB-safe copies | F99–F100 (non-XR part) |
| 0011 venus-win32-nt-memory | Venus opaque Win32 (NT handle) memory export and import | F69–F71 |

### This repository's fixes

| Patch | Fixes | Explained in |
| --- | --- | --- |
| 0012 d3d9-directdraw-hal-caps | The D3D9 UMD answers DirectDraw's `GETD3D7CAPS` and `GETD3D8CAPS` queries, so DirectDraw keeps its HAL and dxdiag reports DDraw and AGP Enabled | [`docs/dxdiag.md`](../../docs/dxdiag.md) |
| 0013 d3d9-write-only-lock-keeps-contents | A write-only D3D9 lock no longer loses the texels the caller did not write (DirectDraw's emulated colour-key and mirror blits) | [`docs/dxdiag.md`](../../docs/dxdiag.md) |
| 0014 d3d9-ddraw-surfaces-are-present-sources | DirectDraw plain surfaces are display targets, so a windowed DirectDraw primary Blt shows | [`docs/dxdiag.md`](../../docs/dxdiag.md) |
| 0015 d3d10umd-hull-shader-vprim-and-indexed-patch-io | Hull shaders with `vPrim`, indexed patch-constant outputs and indexed control-point reads translate (SubD11's device was removed) | [`docs/d3d11-apps.md`](../../docs/d3d11-apps.md) |
| 0016 d3d10umd-immediate-constant-buffer-relative-offset | `icb[r + n]` reads keep their offset (SubD11 rendered purple) | [`docs/d3d11-apps.md`](../../docs/d3d11-apps.md) |
| 0017 yttrium-honour-wireframe-fill-mode | `D3D11_FILL_WIREFRAME` draws wireframe when the host has `fillModeNonSolid` | [`docs/d3d11-apps.md`](../../docs/d3d11-apps.md) |
| 0020 yttrium-gate-feedback-loop-usage-on-format-support | Drops `ATTACHMENT_FEEDBACK_LOOP` usage when the host driver refuses it for the image (NVIDIA rejects it on LINEAR images; dwm crashed before logon) | [`docs/nvidia.md`](../../docs/nvidia.md) |
| 0021 yttrium-d3d11-typed-uav-loads-without-formatless-read | WDDM 2.0 D3D11 devices are created without `shaderStorageImageReadWithoutFormat`, with R32 typed UAV loads only, and GetCaps reports `TypedUAVLoadAdditionalFormats` from the host (Intel ANV; dwm crash-looped) | [`docs/zeus.md`](../../docs/zeus.md) |
| 0022 yttrium-render-shadow-for-linear-colour-with-depth | Depth draws into LINEAR shared back buffers go through an OPTIMAL shadow image and copies, only when the renderer lacks linear colour-attachment support (NVIDIA 610.57.04 raised Xid 69 and hung the guest) | [`docs/xid69.md`](../../docs/xid69.md) |
| 0023 yttrium-shared-colour-optimal-when-linear-refused | Shared colour images (every D3D9 colour texture outside the system-memory pools) use OPTIMAL tiling when the host refuses LINEAR for the exact create-info (NVIDIA allows LINEAR with one mip level only; mipmapped textures sampled black, DEFAULT-pool ones failed to create) | [`docs/nvtex.md`](../../docs/nvtex.md) |
| 0024 d3d9-positiont-vs-for-runtime-fixed-function-ps | Pre-transformed fixed-function draws get a generated vertex shader for any declaration (XYZRHW with diffuse, specular or several texture sets failed with E_FAIL on every GPU) | [`docs/nvtex.md`](../../docs/nvtex.md) |
| 0025 yttrium-packed-16-bit-formats-match-vulkan | D3D R5G6B5 and A1R5G5B5 map to `R5G6B5_UNORM_PACK16` and `A1R5G5B5_UNORM_PACK16` (red and blue were swapped on RADV; R5G6B5 was unsupported, so black, on NVIDIA) | [`docs/nvtex.md`](../../docs/nvtex.md) |
| 0026 d3d9-ddraw-present-sources-in-host-cached-memory | DirectDraw off-screen surfaces that are Blt sources to the primary use host-cached memory, so the KMD's CPU present copy does not read them uncached (13 to 34 windowed Blts per second on AMD CPUs) | [`docs/present.md`](../../docs/present.md) |
| 0027 wgl-present-windows-opaque | WGL windows present opaque: Kopper's swapchain no longer asks for premultiplied alpha, so the compositor ignores the alpha an application leaves in its back buffer (Blender's window was translucent on Windows 10) | [`docs/present.md`](../../docs/present.md) |
| 0030 d3d10umd-domain-shader-indexed-patch-constants | Domain shaders read patch constants with a register index (`dcl_indexrange vpc`; the device was removed at the first draw) | [`docs/ds.md`](../../docs/ds.md) |
| 0031 d3d10umd-output-index-ranges-in-every-stage | Vertex, domain, geometry and pixel shaders and a hull shader's control-point phase may write outputs with a register index (`dcl_indexrange o1.xyzw 4`, `mov o[r0.x + 1]`; the translation failed and the device was removed) | [`docs/shaders2.md`](../../docs/shaders2.md) |
| 0032 d3d10umd-hull-shader-join-phases-read-fork-phase-outputs | Hull shader join phases read the patch constants and tessellation factors their fork phases wrote, with an immediate or a register index (any join phase that read `vpcN` had no NIR, so the device was removed; `vpc1.x` read factor 0) | [`docs/shaders2.md`](../../docs/shaders2.md) |
| 0035 d3d9-hardware-colour-key-and-mirror-blits | DirectDraw source colour-key and mirror blits are drawn on the GPU, matching the emulation layer pixel for pixel, and the HAL reports `DDCKEYCAPS_SRCBLT` and the mirror FX caps (DirectDraw emulated them on the CPU through Lock) | [`docs/ddraw.md`](../../docs/ddraw.md) |
| 0040 d3d9-swvp-processed-vertices-keep-declaration-vs | Fixed-function draws with software vertex processing keep their colours and texture coordinates (every one rendered black) | [`docs/ffp.md`](../../docs/ffp.md) |
| 0041 d3d9-fixed-function-fog | Table fog under a perspective projection uses W (from `UpdateWInfo`), vertex fog is clamped per pixel, and pre-transformed vertices get their fog factor from FOG or the specular alpha | [`docs/ffp.md`](../../docs/ffp.md) |
| 0042 d3d9-fixed-function-points | Points wider than one pixel keep their colours and texture coordinates, get sprite coordinates only with `POINTSPRITEENABLE`, keep their size under perspective, and honour `PSIZE` in `XYZRHW` vertices | [`docs/ffp.md`](../../docs/ffp.md) |
| 0043 d3d9-bump-luminance-formats | `L6V5U5` and `X8L8V8U8` bump maps exist (stored as `R8G8B8A8_SNORM`, converted on upload), so `BUMPENVMAPLUMINANCE` works; `CreateTexture` failed with `D3DERR_NOTAVAILABLE` | [`docs/ffp2.md`](../../docs/ffp2.md) |
| 0044 d3d9-indexed-vertex-blend-palette | `MaxVertexBlendMatrixIndex` is 8, the runtime's matrix palette for indexed vertex blending with hardware vertex processing, instead of 0 | [`docs/ffp2.md`](../../docs/ffp2.md) |
| 0060 d3d10umd-trim-residency-set-leaves-the-context-alone | `TrimResidencySet` no longer flushes the threaded context. The runtime calls it on DXCore's trim-notification thread, and the flush ran a batch outside the context's owner thread; dwm died with a double free (`0xc0000374`) while it recovered a lost device on NVIDIA | [`docs/dwmheap.md`](../../docs/dwmheap.md) |
| 0070 venus-hide-host-access-to-small-bar-heap | The Venus ICD reports a device-local, host-visible type on a heap less than half the largest device-local heap as device-local only, so Vulkan, DXVK, vkd3d-proton and Zink keep host-visible memory out of the host card's BAR1 (246 MiB without resizable BAR, shared by every VM on the card; "can't alloc VA space for mapping") | [`docs/nvva.md`](../../docs/nvva.md) |
| 0085 venus-allocate-from-small-device-local-heap-synchronously | The Venus ICD allocates from a device-local heap smaller than the largest one (the RTX 5090's 246 MiB BAR window) synchronously, so a host out-of-memory reaches the driver's fallback instead of failing the whole context (Unigine OpenGL crashed on NVIDIA) | [`docs/unigl.md`](../../docs/unigl.md) |
| 0086 zink-keep-buffers-mappable-without-visible-vram | Zink keeps buffers it maps directly in host-visible memory when the device has no device-local, host-visible type (needed with 0070) and when an allocation from that heap fails (reachable with 0085), instead of mapping device-local-only memory (`vkr: mem is not exportable`; Unigine Heaven and Valley crashed in `viogpu_wgl.dll`) | [`docs/unigl.md`](../../docs/unigl.md) |
| 0090 yttrium-d3d9-depth-bias-without-float-representation | D3D9 depth bias works on devices without `VK_EXT_depth_bias_control`'s float representation (NVIDIA): it is converted to integer units for the depth format and requested exact. Before, every pipeline with a D3D9 depth bias failed and its draws were skipped | [`docs/unid3d.md`](../../docs/unid3d.md) |

0021 has no mail header. `scripts/pins-table.py` carries its one-line
description.

Applied in order to the pinned tarball, 0001 to 0011 give exactly the codex
tree minus the parts listed below. This was checked by comparing git tree
hashes.

### Left out

| Codex change | Why it was dropped |
| --- | --- |
| `wsi_common_win32.cpp`: load `dxgn.dll`, `d3d1n.dll` and `dcomn.dll` from `System32\YttriumNative` when that directory exists | An experiment with private renamed presentation DLLs, for a global DXGI replacement. The codex session disabled it in its final tested state (`R/VERDICT.md` line 167). |
| `DxgiFns.cpp`: `dxgi_decode_xr` compute shader, its `R10G10B10_XR_BIAS_A2_UNORM` branch in `_Blt`, `Device::dxgi_xr_decode_shader` and its teardown | Exists only for the XR/10-bit presentation side thread (F97–F100). The scaling and sRGB parts of the same `_Blt` change are kept in 0010. |
| `Shader.cpp`: `D3D10UMD_YTTRIUM_COMPUTE_CAPTURE_DIR` bytecode dump | Diagnostic |
| `yttrium_pipeline.c`: `yttrium_capture_compute_*`, `yttrium_trace_compute_buffers`, `D3D10UMD_YTTRIUM_TRACE_COMPUTE`, `…_CAPTURE_COMPUTE_HASH/ORDINAL`, `…_TRACE_COMPUTE_DATA`, `…_SYNC_COMPUTE`, `…_TRACE_INDIRECT_DRAW` | Diagnostics (F18, F24, F39). All of them default to off. |
| `yttrium_shader.c`: `D3D10UMD_YTTRIUM_SHADER_DUMP_ALL_SUCCESS` | Diagnostic (F17 shader capture) |

The codex tree has no PRESENT_EVERY option in Mesa. No YUY2 code is in the
Mesa tree; that work was in vkd3d-proton. The error-level `mesa_loge` lines in
the Venus NT memory path are failure reports, not diagnostics, so 0011 keeps
them.

Which patches have run in a guest, and where:

| Run | Mesa patches in the package | Evidence |
| --- | --- | --- |
| Acceptance, Windows 11 and Windows 10, AMD | 0001 to 0014, 0020 | `docs/acceptance.md` |
| D3D11 samples, Windows 11, AMD | 0001 to 0017, 0020 | `docs/d3d11-apps.md` |
| Zeus Intel and the helios AMD regression, Windows 11 | 0001 to 0014, 0020, 0021 | `docs/zeus.md` |
| nvtex proof, Windows 11, `Nvidia1` and `AMD1` (e2e, probes, SDK samples) | 0001 to 0017, 0020 to 0025 | `docs/nvtex.md` |
| ffp proof, Windows 11, `Nvidia2` and `AMD1` (e2e, probes, `d3d9_ffp`, `Text3D`) | 0001 to 0017, 0020 to 0025, 0030, 0040 to 0042 | `docs/ffp.md` |
| ffp2 proof, Windows 11 on `Nvidia2` and `AMD1`, Windows 10 on `Nvidia2` (e2e, `d3d9_ffp2`, `d3d9_ffp`) | 0001 to 0017, 0020 to 0027, 0030, 0035, 0040 to 0044 | `docs/ffp2.md` |
| unigl proof, Windows 11 on `Nvidia1` and `AMD1` (e2e, Unigine Heaven and Valley OpenGL) | 0001 to 0017, 0020 to 0027, 0030, 0035, 0040 to 0044, 0070, 0085, 0086 | `docs/unigl.md` |
| unid3d proof, Windows 11 on `AMD1`, `Nvidia1` and `Nvidia2` (e2e, `d3d9_depthbias`, Unigine D3D9 and D3D11) | 0001 to 0017, 0020 to 0027, 0030 to 0032, 0035, 0040 to 0044, 0090 | `docs/unid3d.md` |

The nvtex proof run (2026-10-18) has the full series, 0001 to 0025, built by
`scripts/build.sh` at commit `e428846`. Before it, no recorded run had 0015
to 0017 together with 0021. The runs cover the code paths that the probes,
dxdiag and the samples reach, not every fix in 0001 to 0011; for the rest,
the evidence is the codex session's own finding, cited in the patch header.

## Reproducibility

The build is bit-reproducible on this host. Three runs gave identical sha256
for all eight outputs:

1. the first run;
2. a second run on top of the first run's leftovers;
3. a run from a fresh `git clone` of the branch at another path, with an empty
   `out/` (so every download was fetched again) and a re-run of
   `build/toolchain/build.sh --force`.

The build sets `SOURCE_DATE_EPOCH=0`, `-ffile-prefix-map=<work>=/mesa` and
`--no-insert-timestamp`, and mounts the checkout at the fixed path `/w`.
No other machine was tried. Toolchain image
`vmlab-win3d/toolchain:ab524e07f89c`, 2026-10-17. These hashes are for the
series as it was then, 0001 to 0011:

```
a417f90c7e82ffda1f88ef8365da2f3cc225bca282b7b320a365fb0b844df262  mesa/x64/libvulkan_virtio.dll
d74ca3299119ebd886b793bca315bc245ab315e30ee4701c9e3bfa9ed704bc44  mesa/x64/viogpu_d3d10.dll
759c38ea072be579f96a2daba3afcd1c5117f1f667ad4eb11f86909882021605  mesa/x64/viogpu_wgl.dll
f67d39fde82a46dfdbb34581493632e164fe6694310ff125c0c180c91283304a  mesa/x64/virtio_icd.json
a4b09b58804984d91719f75fdacdf69c7f9ff7c731f90a5f615be189d5360800  mesa/x86/libvulkan_virtio.dll
f50cf4dd0b1804599bc6c72832bd07faefd93a7a25fe8b4585c07ff393a349e3  mesa/x86/viogpu_d3d10.dll
eebe236cb7a97e2bdd540c9d68709f132e48aa887d39374b649c655df5691b3e  mesa/x86/viogpu_wgl.dll
69e6a7c23226a0c1c854f9b8ac340cedc361ad2db66fe6aa7733c8e060e03151  mesa/x86/virtio_icd.json
```

The full series, 0001 to 0021, built once on 2026-10-17 with the same
toolchain image, changes only `viogpu_d3d10.dll`. The other six files keep
the hashes above, which fits every patch after 0011 being in the UMD.
This build was not repeated, so its bit-reproducibility is not checked:

```
a43c70d4e56407055e438a6fc831f61ad9c9e458d98ef9937c360a3863fca2fb  mesa/x64/viogpu_d3d10.dll
657e89272ed867033f03a71204d39a30b5063f0e4b275b0a7a7ecd428813dd7e  mesa/x86/viogpu_d3d10.dll
```
