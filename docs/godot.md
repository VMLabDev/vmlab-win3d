# Godot Vulkan on NVIDIA: Xid 69, ErrorCode 0x13d

Godot 4.7.2's Vulkan renderer (Forward+, the official 3D Particles demo)
raised a host **Xid 69, ErrorCode `0x13d`** on the RTX 5090 about 9 s after
start, and then crashed (`docs/compat.md`, F1). This page names the trigger,
gives the evidence and records the fix,
`patches/mesa/0075-wsi-win32-blit-shared-swapchain-when-linear-cannot-render.patch`.
Investigated on 2026-10-19 on helios: NVIDIA 610.57.04, vmlab 0.28.0-alpha,
the patched QEMU, virglrenderer 1.3.0. Evidence is in `docs/evidence/godot/`.

## Result

| Item | Result |
| --- | --- |
| Trigger | A draw into a **LINEAR colour attachment** with a pipeline that has **fragment-shading-rate state** (`VkPipelineFragmentShadingRateStateCreateInfoKHR`, any fragment size). NVIDIA advertises no `COLOR_ATTACHMENT` feature for LINEAR images. The swapchain images that the guest's Win32 WSI gives a Vulkan application are LINEAR. Godot puts shading-rate state on every pipeline when the device has `VK_KHR_fragment_shading_rate`. Its first draw into the swapchain, the boot splash blit, raises the Xid |
| Native repro | `host-tests/linblend.c` on Nvidia1 with no VM: LINEAR target plus pipeline shading-rate state gives the identical Xid (`ce97`, `26c`, data `6`, code `13d`). OPTIMAL, or no shading-rate state, renders. RADV renders every case |
| Owner | Our guest Vulkan stack: the Yttrium DXGI_SHARED present path in Mesa's Win32 WSI (`wsi_common_win32.cpp`). It renders into an image layout that the driver does not advertise. NVIDIA shares the blame, as in [`xid69.md`](xid69.md): `vkGetPhysicalDeviceImageFormatProperties` accepts the image, and the validation layer is silent |
| Fix | `patches/mesa/0075`: when the device's `linearTilingFeatures` for the swapchain format lack what the swapchain usage needs, the application renders into an OPTIMAL image, and the present copies it into the LINEAR shared image. AMD and Intel keep the zero-copy path |
| Not the cause | BAR1 exhaustion (worker `nvva`): the Xid came with nvva's 0070/0071 in the guest too, with no BAR1 use, and there were no BAR1 errors at the two F1 times. Worker `ei` (ExecuteIndirect) is unrelated |
| Proof | See [Proof](#proof) |

## The Xid, decoded

```
NVRM: Xid (PCI:0000:01:00): 69, pid=4094771, name=vkr-ring-40, Class Error: channel 0x000000dd, Class 0000ce97, Offset 0000026c, Data 00000006, ErrorCode 0000013d
```

- Class `0xce97` is the Blackwell 3D class. Method `0x026c` is
  `NVCE97_DRAW_INDEX_BUFFER_BEGIN_END_B`, the index count of an indexed draw.
- Data `0x6` is that count: one quad.
- As in [`xid69.md`](xid69.md), the method is only where the GPU checks the
  state it has latched. Xid69's ErrorCode `0x9c` is LINEAR colour with depth.
  This one, `0x13d`, is LINEAR colour with a shading-rate state.

## How it was found

1. **A clean repro.** The first two runs on Nvidia1 failed differently.
   The card's 256 MiB BAR1 was full (four VMs), so `vkAllocateMemory` of
   the small-BAR type failed and Godot could not create its swapchain. That
   is worker `nvva`'s bug. The guest bundle was then rebuilt with nvva's
   0070/0071, so the guest made no BAR1 allocations, and the third run
   reproduced F1 exactly: the Xid 7.5 s after the Godot context's first command, Data `6`,
   ErrorCode `0x13d` (`diag-nvidia1/kernel-xid.txt`).
2. **Which submission.** The lab ran on an isolated vmlab supervisor whose
   host config (`diag-nvidia1/host-config.wcl`) set `qemu_env` for Nvidia1.
   The render server was a diagnostic build
   (`patches/host/virglrenderer/diagnostic/0040`). That build records every Venus
   command of the Godot context (`VKR_TRACE`, `VKR_TRACE_APP`) and waits for
   every queue submission (`VKR_SYNC_SUBMIT`). The validation layer
   (`VKR_DEBUG=validate`, Khronos 1.4.363) was also loaded. Submission 6, with
   command buffers 1732 and 24, returned `VK_ERROR_DEVICE_LOST`
   (`diag-nvidia1/render-server-log.txt`).
3. **Which command.** Command buffer 1732 holds a single render pass, the
   only one Godot had recorded so far (`diag-nvidia1/trace-godot-context.txt`,
   records 3301 to 3313):
   `vkCmdBeginRenderPass` 1280×720, `vkCmdBindPipeline` 0x6bc,
   `vkCmdBindIndexBuffer` (UINT16), `vkCmdPushConstants` (96 bytes),
   `vkCmdBindDescriptorSets`, `vkCmdDrawIndexed(6, 1, 0, 0, 0)`. This is
   Godot's boot splash blit onto the swapchain image.
4. **What the draw used** (`diag-nvidia1/key-records-fullhex.txt`):
   - Render pass 0x697 has one `B8G8R8A8_UNORM` colour attachment, loaded
     with `CLEAR`, and no depth attachment and no shading-rate attachment.
   - Its framebuffer's view is of image 0x691: `B8G8R8A8_UNORM`, 1280×720,
     **`tiling = LINEAR`**, usage `0x17`, `ALIAS`. Its memory is a
     dedicated import of a guest resource.
   - Pipeline 0x6bc blends (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`). Like every
     Godot pipeline in the trace, it chains
     `VkPipelineFragmentShadingRateStateCreateInfoKHR` with fragment size
     4×4 and combiner ops `KEEP`, `REPLACE`.
   - The device was created with `VK_KHR_fragment_shading_rate` and its
     features structure.
5. **Validation is silent.** For this context it reported only the
   long-standing dma-buf bind messages and one `synchronization2` message,
   which came after the device was lost.
6. **Without a VM.** `host-tests/linblend.c` makes the same draw on the host.
   One indexed quad (6 indices) goes into a 1280×720 `B8G8R8A8_UNORM` image,
   and command-line options vary one thing at a time
   (`host-tests/out/`):

   | Colour target | Blend | Pipeline shading-rate state | NVIDIA 610.57.04 | RADV |
   | --- | --- | --- | --- | --- |
   | OPTIMAL | alpha | none or 4×4 | renders | not run |
   | LINEAR, system or device memory | none or alpha | none | renders | renders |
   | LINEAR, imported from a second device as a dma-buf, as vkr does | none or alpha | none | renders | not run |
   | LINEAR | none | none; device feature on | renders | not run |
   | LINEAR | none | 1×1, 2×2 or 4×4 | `VK_ERROR_DEVICE_LOST`, Xid 69 (`26c`, data `6`, code `13d`) | renders (4×4) |
   | LINEAR, imported | alpha | 4×4 (Godot's draw) | `VK_ERROR_DEVICE_LOST`, the same Xid | renders |

   Blending, the import and the memory placement do not matter. The
   shading-rate state on a pipeline that draws into a LINEAR colour target
   does, even at 1×1.

## Why the swapchain image is LINEAR

`vulkaninfo` lists the guest's Venus device with Mesa's Win32 WSI. On a
Yttrium guest the WSI presents through the DXGI_SHARED path
(`wsi_create_dxgi_shared_image_mem`):

- the WSI creates a shared D3D11 texture through the Yttrium D3D11 UMD;
- Yttrium makes it a LINEAR Vulkan image in exportable memory, because
  shared colour images are LINEAR (`yttrium_venus_shared_colour_tiling`;
  on NVIDIA the exact create-info is accepted, so 0023 keeps LINEAR);
- the WSI imports that memory into the application's device and binds its
  own LINEAR `ALIAS` image to it, with the same usage. The tiling is
  forced to LINEAR on both sides, because two OPTIMAL images from two
  devices need not agree on their swizzle (the comment in
  `wsi_dxgi_shared_configure_image` records a RADV case);
- at present, D3D11 copies the texture into the DXGI swap chain's buffer.

The application renders straight into that LINEAR image. NVIDIA's
`linearTilingFeatures` lack `COLOR_ATTACHMENT` for every format
(`docs/evidence/xid69/host-tests/out/formatfeatures-nvidia1.txt`), yet
plain draws work (dwm and the repo's Vulkan probe). The
hardware rejects the render-target setup only when more state is latched:
depth (0x9c, [`xid69.md`](xid69.md)) or a shading-rate state (0x13d, here).

Godot is the first application in the sweep that sets shading-rate state.
In the trace, every graphics pipeline it creates chains
`VkPipelineFragmentShadingRateStateCreateInfoKHR`, including the blit
pipelines it never uses with a shading-rate attachment. That makes Godot
fail on its very first frame. Any Vulkan application that draws with depth
straight into its swapchain image hits the 0x9c variant of the same fault.

## The fix: `patches/mesa/0075`

The patch changes `src/vulkan/wsi/wsi_common_win32.cpp`, `wsi_common.c`
and `wsi_common_private.h`. All of it is inside `HAVE_YTTRIUM`, except the
blit command's layout handling, which only the DXGI_SHARED type reaches.

- At swapchain creation, `wsi_dxgi_shared_linear_unusable()` reads the
  device's format properties for the swapchain format. It returns true when
  the swapchain usage includes `COLOR_ATTACHMENT` and `linearTilingFeatures`
  lacks `COLOR_ATTACHMENT`, or the usage includes `STORAGE` and the features
  lack `STORAGE_IMAGE`. The DXGI_SHARED image parameters then ask for
  `WSI_SWAPCHAIN_IMAGE_BLIT`, the mode Mesa's DXGI (D3D12) path already uses.
- In that mode, `wsi_dxgi_shared_configure_image()` leaves the
  application's image as `wsi_configure_image()` makes it: OPTIMAL, with the
  application's usage plus `TRANSFER_SRC`, in device-local memory.
  `wsi_create_dxgi_shared_image_mem()` creates the D3D11 texture as before,
  creates the LINEAR `ALIAS` image as the blit destination, imports the
  shared memory for it, and allocates dedicated memory for the application's
  image.
- At present, the common WSI submits the per-image blit command buffer,
  which copies the OPTIMAL image into the LINEAR one, and signals the
  per-image fence. `wsi_win32_queue_present_dxgi_shared` already waits for
  that fence before D3D11 copies the texture to the swap chain. The blit
  destination is not a presentable image, so its barriers use `UNDEFINED`
  and `GENERAL` instead of `PRESENT_SRC_KHR`.

On RADV and ANV, `linearTilingFeatures` has `COLOR_ATTACHMENT`, so nothing
changes there. On NVIDIA every Vulkan swapchain image now costs one
image copy per present. Masking `VK_KHR_fragment_shading_rate` on NVIDIA
would also have stopped Godot's Xid. It was not chosen, because it leaves
depth draws into the swapchain (Xid 0x9c) and any other latched state that
NVIDIA rejects on pitch-linear targets.

## Proof

All runs used fresh labs from `x86_64/windows-11@26100.1742.5`, on the
host config with no debug variables. The bundle was built from this branch
at `d58167a` (AMD1 Godot runs) or `d15ccaa` (the rest). The two differ only
in a comment line of the diagnostic render-server patch, which no build
applies. The patch series was main's (0001 to 0044) plus 0075. The Godot runs
are `tests/apps/compat/run.sh --only godot-d3d12,godot-vulkan,godot-opengl`
(150 s each, then closed with WM_CLOSE). The acceptance runs are
`tests/e2e/run.sh --template windows-11`. Evidence is under
`docs/evidence/godot/proof/<gpu>/`, with the driver log in `proof.log`.

| Check | Nvidia1 (RTX 5090) | AMD1 (RADV) |
| --- | --- | --- |
| Godot Vulkan (Forward+) | clean exit, 58.4 fps, renders (`compat/godot-vulkan/`) | clean exit, 59.9 fps |
| Godot D3D12 | clean exit, 59.7 fps | clean exit, 59.5 fps |
| Godot OpenGL (Compatibility) | clean exit, 57.2 fps | clean exit, 64.7 fps |
| `tests/e2e/run.sh` Done checks | 12 of 12 (`e2e/`) | 12 of 12 (`e2e/`) |
| Host Xid during the runs | none (no Xid line in the kernel log from 06:40 to the end of the runs) | not applicable |

Before the fix, every Godot Vulkan run that got as far as its first frame
raised the Xid: twice on Nvidia2 in the compat sweep, and once on Nvidia1 in
the diagnostic run. The two earlier diagnostic runs failed sooner, because
BAR1 was full. In the AMD1 Godot frames the Windows Start menu was
open over the demo window. The demo rendered and animated behind it, and the
FPS figures come from Godot's own log.

MERGED-PLACEHOLDER

## What remains

- **Upstream.** 0075 belongs with 0011 and the DXGI_SHARED path in
  `arehnman/virtio-win-mesa`. The NVIDIA report from [`xid69.md`](xid69.md)
  gains a second case: `host-tests/linblend.c`.
- **Other LINEAR render targets.** Yttrium still renders depth-less D3D
  draws into LINEAR shared images on NVIDIA, outside what the driver
  advertises ([`xid69.md`](xid69.md)). A D3D11 or D3D12 application whose
  draws carry shading-rate state into its back buffer would hit this Xid
  there. D3D11 has no shading rates, and vkd3d-proton renders D3D12 into its
  own images, so no such case is known.

## Evidence index (`docs/evidence/godot/`)

| Path | What it shows |
| --- | --- |
| `diag-nvidia1/` | The diagnostic run: host config with `qemu_env`, the kernel Xid, the render-server log of the Godot context (sync-submit results, validation), the decoded command trace of that context (`trace-godot-context.txt`; argument bytes truncated to 64), the full hex of the records named above, Godot's log, the UMD log and a console frame |
| `host-tests/` | `linblend.c` and its shaders, `build.sh`, and the outputs on Nvidia1 and RADV (`out/`) |
| `tools/vkr-trace-dump.py` | Prints a `VKR_TRACE` file |
| `tools/build-diag-render-server.sh` | Builds the diagnostic render server (1.3.0 + `patches/host/virglrenderer/*.patch` + `diagnostic/0040`) in Docker |
