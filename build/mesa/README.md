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

The codex session left its fixes uncommitted in `R/mesa`, and its exported
`R/mesa-driver.patch` is stale (prior-art section 3). The series in
`patches/mesa/` was re-exported from `git diff 374c3022` of that working tree.
It had 43 changed files, +1793/−161, and two untracked files. The two untracked
files are the mixed-case header symlinks that `prepare-build.py` creates, so
they are build output, not source. The diff was then split by topic. Each
patch has a header that says what it fixes and which prior-art finding it
comes from. The patches apply in name order.

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

Applied in order to the pinned tarball, the series gives exactly the codex
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

None of these patches has been run in a guest since the re-export. The
evidence for each is the codex session's own finding, cited in the patch
header.

## Reproducibility

Two consecutive runs on the same host gave bit-identical outputs (all eight
files). The second run started with the first run's leftovers in place. The
build sets `SOURCE_DATE_EPOCH=0`, `-ffile-prefix-map=<work>=/mesa`,
`--no-insert-timestamp` and a fixed mount path (`/w`).
