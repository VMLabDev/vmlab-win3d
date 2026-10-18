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
- **0012 to 0017 and 0020 to 0025** are this repository's own fixes, found by
  the acceptance and host runs. Each header names the document with the
  evidence. The series skips 0018 and 0019.

0005 to 0010 and every patch from 0012 on change `viogpu_d3d10.dll`, the
D3D9/D3D10/D3D11 UMD (the `d3d10umd` frontend and the Yttrium winsys). 0001
to 0003 are build and utility fixes that every DLL shares, 0004 is in WGL,
and 0011 is in the Venus ICD.

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

No recorded run has the full current series, 0015 to 0017 together with
0021. The two sets touch different code (shader translation and fill mode
against device creation and typed UAV loads). The full series applies to the
pinned tarball and builds (`build/mesa/build.sh` on 2026-10-17, hashes under
[Reproducibility](#reproducibility)), but that build has not run in a guest. The runs
cover the code paths that the probes, dxdiag and the samples reach, not every
fix in 0001 to 0011; for the rest, the evidence is the codex session's own
finding, cited in the patch header.

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
