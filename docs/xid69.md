# Xid 69 on the RTX 5090: depth draws into a LINEAR back buffer

The DirectX SDK (June 2010) D3D9 `BasicHLSL` sample hung the Windows guest on
both RTX 5090s, and each time the host kernel logged an Xid 69. This page
names the trigger, gives the evidence and records the fix,
`patches/mesa/0022-yttrium-render-shadow-for-linear-colour-with-depth.patch`.
Investigated on 2026-10-18 on helios: NVIDIA 610.57.04, vmlab 0.28.0-alpha,
QEMU 11.1.1 with the egl-headless patch, virglrenderer 1.3.0. Evidence is in
`docs/evidence/xid69/`.

## Result

| Item | Result |
| --- | --- |
| Trigger | A render pass that has a **LINEAR colour attachment and a depth attachment**. NVIDIA advertises no `COLOR_ATTACHMENT` feature for LINEAR images of any format. Yttrium renders straight into the shared back buffer, which is LINEAR. Without depth, the GPU tolerates this. With depth, the first draw raises the Xid |
| Owner | Our guest UMD: the Yttrium venus2 winsys (upstream `arehnman/virtio-win-mesa`). It uses a format feature that the driver does not advertise. NVIDIA shares some blame: `vkGetPhysicalDeviceImageFormatProperties` accepts LINEAR with `COLOR_ATTACHMENT` usage, so its two queries disagree |
| Fix | `patches/mesa/0022`: such passes render into an OPTIMAL shadow image of the target, and the draw copies the target into the shadow before the pass and back after it. AMD, Intel and depth-less draws are unchanged |
| BasicHLSL on Nvidia1 | Runs, with no hang and no Xid. **The `tiny` model renders black**, and so does the disc in `ParallaxOcclusionMapping`. This is a separate NVIDIA texture defect, not investigated ([open items](#open-items)) |
| BasicHLSL on AMD1 | Unchanged, renders as before |
| Probes | See [Proof](#proof) |

## The Xid, decoded

```
NVRM: Xid (PCI:0000:01:00): 69, pid=<render server>, name=vkr-ring-117, Class Error: channel 0x00000052, Class 0000ce97, Offset 0000026c, Data 000000a8, ErrorCode 0000009c
```

- Class `0xce97` is the Blackwell 3D class. In Mesa's class headers
  (`src/nouveau/headers/nvidia/classes/clce97.h`), method `0x026c` is
  `NVCE97_DRAW_INDEX_BUFFER_BEGIN_END_B`, the index count of an indexed draw.
- Data `0xa8` is that count, 168 indices. Every one of the nine Xids that
  day carried the same count (`kernel-nvrm-2026-10-18.txt`).
- A non-indexed draw fails the same way, at `0x0274`
  (`DRAW_VERTEX_ARRAY_BEGIN_END_B`) (`host-tests/out/lindepth-nvidia1.txt`).
- The method is only where the GPU checks the state it has latched. The
  rejected state is the render target setup, not the draw.

## How it was found

1. **Which draw.** BasicHLSL ran on AMD1 with the D3D9 DDI trace on
   (`D3D10UMD_D3D9_TRACE=1`, `amd-trace/`). Every frame starts with
   `Clear(target | zbuffer)`. The first draw after it is
   `DrawIndexedPrimitive count=56` over 58 vertices, 168 indices: the DXUT
   light-direction arrow. The device has a `D3DFMT_D24X8` depth buffer.
2. **Not the indexed draw itself.** `src/d3d9_indexed.cpp` draws 168-index
   batches in eight forms: static 16- and 32-bit index buffers, the UP form,
   base vertex, strips, and dynamic index and vertex buffers with
   `NOOVERWRITE`. All ran on Nvidia1 with no Xid
   (`guest-bisect/results.txt`). The repo's D3D9 probe had passed on NVIDIA
   for the same reason: it has no depth buffer.
3. **Depth.** The same probe with a depth buffer (D16, D24S8 or D24X8) hung,
   and the host logged the identical Xid (`ce97`, `26c`, `a8`, `9c`) each time.
4. **Validation is silent.** The render server ran under
   `VKR_DEBUG=validate` with Khronos validation 1.4.363. The failing contexts
   logged nothing about the draw, only the long-standing dma-buf
   `vkBindImageMemory` messages. The layer trusts
   `vkGetPhysicalDeviceImageFormatProperties`, and that query accepts the image.
5. **Without a VM.** `host-tests/lindepth.c` is a plain host program on
   Nvidia1. It renders 28 quads into a 640×480 `B8G8R8A8_UNORM` colour image
   in one render pass (`host-tests/out/`):

   | Colour tiling | Depth | NVIDIA 610.57.04 | RADV |
   | --- | --- | --- | --- |
   | LINEAR | none | renders | renders |
   | LINEAR | D16 | `VK_ERROR_DEVICE_LOST`, Xid 69 (`26c`/`274`, data `a8`, code `9c`) | renders |
   | OPTIMAL | D16 | renders | not run |

6. **The format feature.** `host-tests/formatfeatures.c` shows that NVIDIA's
   `linearTilingFeatures` lack `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` for
   `B8G8R8A8_UNORM`, `R8G8B8A8_UNORM`, `B8G8R8A8_SRGB`, `R8_UNORM` and
   `R16G16B16A16_SFLOAT`. RADV sets it for all of them
   (`host-tests/out/formatfeatures-*.txt`).

## Why the guest renders into LINEAR images

The venus2 winsys creates shared single-sample colour images with
`VK_IMAGE_TILING_LINEAR`, so that the Win32 WSI, importers and scanout can
reproduce their layout (`yttrium_venus2_resource.c`, the `shared_colour`
case). Every D3D9 back buffer is such an image: it is created with
`PIPE_BIND_SHARED | DISPLAY_TARGET | RENDER_TARGET`. The draw path
(`yttrium_venus2_pipeline_init`) makes an image view of that image and
attaches it as a colour attachment with `LOAD`/`STORE`. Nothing checks
`linearTilingFeatures`.

That is why the bug hid. dwm, the probes and the D3D9 UI all draw without
depth. The hardware renders into a pitch-linear target as long as no Z
buffer is bound, so they work. Any D3D9 or DXGI application that draws with a
depth buffer into its back buffer hits the Xid on NVIDIA. Most 3D games do.

## The fix: `patches/mesa/0022`

The patch changes `yttrium_venus2_impl.c`, `yttrium_venus2_batch.c`,
`yttrium_venus2_resource.c`, `yttrium_venus.h` and `yttrium_venus2_private.h`.

- `yttrium_venus_create_image_tiled()` records the tiling on the resource.
- `yttrium_venus_color_render_shadow()` decides that a colour attachment
  needs a shadow when all of these hold:
  - the render pass has depth;
  - the image is LINEAR, 2D, single-sample and not a feedback-loop attachment;
  - the renderer's `linearTilingFeatures` lack `COLOR_ATTACHMENT` for the
    image's or the view's format. The answer is cached per format.
- `yttrium_venus_ensure_render_shadow()` creates an OPTIMAL image once per
  resource. It has the same format, extent, levels and layers, is
  `MUTABLE_FORMAT`, has colour-attachment and transfer usage, and lives in
  device-local memory. The pipeline's colour view names this image.
  The shadow is retired and destroyed with the resource.
- Around the draw's own render pass, `yttrium_venus_cmd_render_shadow_copies()`
  works on the target's level and layers. It copies LINEAR → shadow before
  `vkCmdBeginRenderPass` and shadow → LINEAR after `vkCmdEndRenderPass`,
  with barriers on both sides. The LINEAR image therefore stays the only copy
  that presentation, sharing, readback and later passes see.
- Draws that use a shadow are not deferred into the render-pass batch. Its
  replay has no place for the copies, so the draw takes the direct path. Any
  batch already queued is flushed first, as for any other draw that cannot
  be batched.

On RADV and ANV the predicate is false, so nothing changes there. The cost on
NVIDIA is two image copies of the target level per depth draw. BasicHLSL ran
at the vsync rate, and ParallaxOcclusionMapping showed 31 fps at 800×600 with
vsync off.

Draws without depth still render into LINEAR images on NVIDIA. That is also
outside what the driver advertises, but it works on this driver, and dwm
depends on it. Routing them through the shadow would cost two copies per
pass. Revisit that if a future driver refuses it.

## Proof

Both runs used fresh labs from `x86_64/windows-11@26100.1742.5`. The bundle
was built at commit `025a680`, which is `patches/mesa` 0001 to 0022
(`after/*/e2e-build.txt`). Each run did `tests/e2e/run.sh --host-gpu <gpu>
--keep`, then `Run-AllProbes.ps1`, then the samples through `run-sample.sh`
for 30 s each (25 s for POM). The host config matched helios's: `Nvidia1`
with the patched QEMU as `qemu_binary`, and no debug variables.

| Check | Nvidia1 (RTX 5090) | AMD1 (RADV) |
| --- | --- | --- |
| `tests/e2e/run.sh` Done checks | 12 of 12 (`after/nvidia1/e2e-results.txt`) | 12 of 12 (`after/amd1/e2e-results.txt`) |
| `Run-AllProbes.ps1` | 10 of 10 pass, exit 0 (`run-allprobes.txt`) | 10 of 10 pass, exit 0 |
| BasicHLSL x64 | runs 30 s, then closed; no Xid; model black (`console-BasicHLSL-x64.png`) | renders, textured |
| BasicHLSL x86 | runs 30 s, then closed; no Xid; model black (`console-BasicHLSL-x86.png`) | renders, textured |
| ParallaxOcclusionMapping x64 | runs; no Xid; disc black (`console-POM-x64.png`) | renders, textured |
| Host Xid during the runs | none (`kernel-nvrm-*.txt` empty; the last Xid that day was the 16:40 "before" run) | not applicable |

Before the fix, in the same kind of lab, BasicHLSL x64 never showed its
window, and the host logged the Xid four seconds after start
(`before/`). The GPU recovered after every Xid in this study: `nvidia-smi`
reported both cards at P8 afterwards, with no Xid 79. The D3D9 probe ran at
18 to 20 fps on Nvidia1 (about 22 in the acceptance run before 0022) and at 28
to 30 on AMD1. It draws without depth, so 0022 adds only a cached format check
to it. The small drop on Nvidia1 was not investigated.

## Open items

- **Textured meshes render black on NVIDIA.** With 0022, BasicHLSL's `tiny`
  and ParallaxOcclusionMapping's disc render as black silhouettes on Nvidia1.
  On AMD1 they are textured. Untextured geometry, the text and the DXUT UI
  render correctly, and all of these go through the same shadow passes. The
  black meshes are the ones sampling mipmapped textures: `tiny_skin` is
  `D3DFMT_X8R8G8B8` with 9 levels, in `D3DPOOL_MANAGED`. Before 0022 this
  could not be seen, because these samples hung first. The cause is not
  investigated. A first fixed-function texture probe failed its draw with
  `E_FAIL` on both GPUs, because the UMD does not support that
  fixed-function setup, and it was dropped.
- **BasicHLSL x86** exited at once with `0xC000012F`
  (`STATUS_INVALID_IMAGE_NOT_MZ`) in the first patched lab, before loading any
  graphics module. The x86 D3D9 probe ran in the same lab with the same
  UMD. In the fresh proof lab, BasicHLSL x86
  ran normally, so this was a fault in that lab's DXSETUP or VC++ staging,
  not in the UMD.
- **Upstream.** 0022 belongs in `arehnman/virtio-win-mesa`, with 0020. A
  report to NVIDIA could ask for one of two things: refuse
  `COLOR_ATTACHMENT` usage on LINEAR images in
  `vkGetPhysicalDeviceImageFormatProperties`, to match the format features,
  or support linear colour attachments next to depth. The minimal repro is
  `host-tests/lindepth.c`.
- `build/mesa/README.md` lists the patch series and is not in this slice. It
  needs a 0022 row.

## Evidence index (`docs/evidence/xid69/`)

| Path | What it shows |
| --- | --- |
| `kernel-nvrm-2026-10-18.txt` | Every Xid that day on helios: the acceptance runs, the guest bisect, BasicHLSL before the fix and the host repro |
| `amd-trace/` | One BasicHLSL frame of D3D9 DDI calls on AMD1, and its D24X8 depth surface |
| `guest-bisect/results.txt` | The bisect-probe table: forms, depth formats, results, Xids |
| `before/` | BasicHLSL x64 on Nvidia1 without 0022: no window, and the Xid |
| `host-tests/` | `lindepth.c` and `formatfeatures.c` with their outputs on NVIDIA and RADV (`out/`). `build.sh` builds `lindepth`; `formatfeatures.c` names its one-line build |
| `src/` | The guest bisect probe `d3d9_indexed.cpp`; `build.sh` builds it in the project toolchain image |
| `run-sample.sh` | Runs one program in a kept lab's desktop with console captures and the host NVRM lines |
| `after/` | The proof runs below |
