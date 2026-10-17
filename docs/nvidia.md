# Venus on the NVIDIA RTX 5090

This page explains why the package failed on this host's RTX 5090s and what
fixes it. It also records how the failure was reproduced, what was tried, and
who owns each fix. Every claim cites a file under `docs/evidence/nvidia/`.
Date: 2026-10-17.

## Result

It works. With the fixes below, a Windows 11 guest on the first RTX 5090
(`0000:01:00.0`, `renderD128`) passes all ten probes: Vulkan, D3D9, D3D11,
D3D12 and OpenGL, each as x64 and x86. `vulkaninfo` lists the Venus device,
and the vmlab console shows the desktop and every probe.

Two separate faults stopped it. Neither is a bug in the NVIDIA driver.

| # | Symptom | Failing call | Fix | Owner |
| --- | --- | --- | --- | --- |
| 1 | dwm crashes before logon (`0x889800c0`); Vulkan and OpenGL presents fail | `vkCreateImage` and `vkGetPhysicalDeviceImageFormatProperties2` return `VK_ERROR_FORMAT_NOT_SUPPORTED` for LINEAR images with `VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT` | `patches/mesa/0020-yttrium-gate-feedback-loop-usage-on-format-support.patch` (guest, in the package) | Mesa: the Yttrium venus2 winsys (`arehnman/virtio-win-mesa`) |
| 2 | Black vmlab console, although the guest renders | QEMU egl-headless: `glEGLImageTargetTexture2DOES(GL_TEXTURE_2D)` gives `GL_INVALID_OPERATION` for the LINEAR scanout dma-buf | `patches/host/qemu/0001-ui-egl-headless-external-only-dmabuf-scanouts.patch` (a lab-local QEMU 11.1.1) | QEMU upstream (`ui/egl-helpers.c`, `ui/egl-headless.c`); vmlab must be able to run that QEMU |

Fix 1 is in the package. Fix 2 is a host component. It runs only for labs
started through `labs/nvidia/vmlab-nv.sh`, and nothing is installed on the
host (decision D3).

## Reproduction

The host stack is frozen: QEMU 11.1.1, virglrenderer 1.3.0, Mesa 26.2.2, and
NVIDIA 610.57.04. The runs recorded here used vmlab 0.24.0-alpha, with the
lab naming the GPU as `gpu { mode = "vulkan" address = "0000:01:00.0" }`
(`04-guest-fix/qemu-argv-env.txt` records what vmlab gave QEMU). vmlab
0.25.0-alpha moved the address into the host config; the steps below are
for 0.25.

1. Build the package with `scripts/build.sh`. A bundle built before
   `patches/mesa/0020` reproduces fault 1.
2. Use the lab `labs/nvidia/vmlab.wcl`. It is the plain vmlab 0.25 form, with
   `gpu { mode = "vulkan" host_gpu = "Nvidia1" }` and
   `secure_boot = false`. The host config, `~/.config/vmlab/config.wcl`,
   declares `gpu "Nvidia1" { address = "0000:01:00.0" default = true }`
   (`docs/host.md`, "Update: vmlab 0.25"). vmlab then passes
   `-display egl-headless,rendernode=/dev/dri/renderD128` and puts
   `VK_LOADER_DRIVERS_SELECT=*nvidia*` in QEMU's environment.
3. `labs/nvidia/vmlab-nv.sh up` brings the lab up under an isolated
   supervisor in `.scratch/labs/nvidia-a`. With no variables set, it runs the
   host's own QEMU and render server.
4. Install the bundle with
   `VMLAB=labs/nvidia/vmlab-nv.sh scripts/install-into-vm.sh .scratch/labs/nvidia-a win11`.
   Then set up the test desktop as `scripts/test-package-in-lab.sh` step 00
   does.

Without the fixes, the install itself succeeds, because it runs as SYSTEM.
After its reboot, the following happens:

- dwm crashes in `dwmcore.dll` with `0x889800c0`, again after each restart,
  and nobody gets logged on (`01-repro/guest-app-errors.txt`).
- The console is black (`01-repro/console.png`).
- The render server logs one failed image per Venus context, then a fatal
  decoder state (`01-repro/qemu-render-server.log`):

  ```
  vkr: vkCreateImage failed: ret -11 type 1 fmt 44 1280x800x1 mips 1 layers 1 samples 1 tiling 1 usage 0x80017 flags 0x8 ...
  vkr: failed to look up object 25 of type 10
  vkr: vkGetImageMemoryRequirements resulted in CS error
  vkr: ring_submit_cmd: vn_dispatch_command failed
  vkr: submit_cmd: early bail due to fatal decoder state
  vkr: destroying context 13 (yttrium) with a valid instance
  ```

  The stock render server prints only the last five lines; the prior art and
  `docs/host.md` (`good-none`) saw the same ones. The `vkCreateImage failed`
  line, with its create-info, comes from the diagnostic render server
  (`patches/host/virglrenderer/0001`), which is otherwise unchanged.

## Fault 1: an image usage NVIDIA refuses on LINEAR images

### The failing call

`vkCreateImage` gets this create-info:

| Field | Value |
| --- | --- |
| format | `B8G8R8A8_UNORM` (44), also `R8_UNORM` (9) |
| tiling | `VK_IMAGE_TILING_LINEAR` |
| usage | `0x80017`: `TRANSFER_SRC`, `TRANSFER_DST`, `SAMPLED`, `COLOR_ATTACHMENT`, and **`ATTACHMENT_FEEDBACK_LOOP_BIT_EXT`** |
| flags | `MUTABLE_FORMAT` (`0x8`); shared surfaces add `ALIAS` (`0x400`) |
| pNext | none |

NVIDIA 610.57.04 returns `VK_ERROR_FORMAT_NOT_SUPPORTED` for it, from both
`vkGetPhysicalDeviceImageFormatProperties2` and `vkCreateImage`. The same
image without the feedback-loop bit is fine, and so is the same usage with
OPTIMAL tiling. RADV accepts every case. A native test program on the host,
with no VM, shows this (`host-tests/imgtest.c`, `imgtest-nvidia.txt`,
`imgtest-radv.txt`):

```
fmt 44 tiling 1 usage 0x80017 flags 0x8 1280x800: formatprops -11 (maxExtent 0) createImage -11
fmt 44 tiling 0 usage 0x80017 flags 0x8 16x16: formatprops 0 (maxExtent 32768) createImage 0
fmt 44 tiling 1 usage 0x17 flags 0x8 1280x800: formatprops 0 (maxExtent 32768) createImage 0
```

The Vulkan validation layer agrees. With `VKR_DEBUG=validate` on the render
server, it logs `VUID-VkImageCreateInfo-imageCreateMaxMipLevels-02251`: the
image-format query returned `VK_ERROR_FORMAT_NOT_SUPPORTED` for that
create-info. This is not a driver bug. Vulkan lets a driver refuse a usage
for a given tiling.

### Why the guest does not notice

The D3D9, D3D10 and D3D11 UMD is Yttrium's Gallium driver. Its venus2 winsys
(`src/gallium/winsys/yttrium/gdi/yttrium_venus2_resource.c`) encodes Venus
commands itself:

- `yttrium_venus_feedback_loop_usage()` adds
  `ATTACHMENT_FEEDBACK_LOOP_BIT_EXT` to every sampled colour or depth
  attachment when the device has `VK_EXT_attachment_feedback_loop_layout`.
- Shared single-sample colour images are LINEAR, so that the Win32 WSI and
  importers can reproduce their layout.
- `yttrium_venus_create_image_tiled()` sends `vkCreateImage` **without a
  reply**, so the guest never sees the error.

The host has no image under that object ID. The next
`vkGetImageMemoryRequirements` names it, which is a fatal CS error, so the
renderer destroys the whole context. dwm's D3D device goes with it.

The same refusal also breaks a second path.
`yttrium_venus2_sampled_image_supported()` sends the refused usage to the
image-format query, gets a failure, and rejects shared textures. In the Win32
WSI this shows as `DXGI_SHARED CreateTexture2D failed (HRESULT 0x8007000e)`
and then `vkCreateSwapchainKHR` `VK_ERROR_OUT_OF_DEVICE_MEMORY`, so the
Vulkan and OpenGL probes fail
(`02-render-server-patch/wsi-createtexture2d-failure.txt`,
`probes-table-createimage-only.txt`).

### The fix: `patches/mesa/0020`

Before keeping the feedback-loop bit, the winsys now asks
`vkGetPhysicalDeviceImageFormatProperties2` about the exact create-info:
format, type, tiling, final flags and usage. If the driver refuses, the
winsys drops the bit. It does this in `yttrium_venus_create_image_tiled()`
and in `yttrium_venus2_sampled_image_supported()`. The resource records the
usage it was actually created with, and the draw path already refuses a
feedback-loop layout for an image without that usage
(`yttrium_venus2_impl.c`, the colour and depth attachment checks). Answers
are cached per renderer, format, type, tiling, flags and usage, because each
query is a synchronous round trip. On RADV the query succeeds and nothing
changes.

### Proof

| Run | Host components | Result | Evidence |
| --- | --- | --- | --- |
| NVIDIA, guest fix | **stock** `/usr/lib/virgl_render_server`, patched QEMU (fault 2) | all 10 probes pass; vulkaninfo lists Venus (NVIDIA); console shows the desktop and every probe, also after a guest reboot; no `vkr:` errors | `04-guest-fix/` |
| AMD iGPU, guest fix (regression) | plain vmlab 0.24, host QEMU and render server, lab `win3d-nvidia-amdreg` on `0000:72:00.0` (now `labs/nvidia/vmlab-amd-regression.wcl`, `host_gpu = "AMD1"` on 0.25) | all 10 probes pass; vulkaninfo lists Venus (RADV); console shows every probe | `05-amd-regression/` |

`04-guest-fix/qemu-argv-env.txt` records the QEMU binary, its environment
(no `RENDER_SERVER_EXEC_PATH`) and the render server it spawned. The dxdiag
summaries report `hardware_acceleration: false`, because `DDraw Status` is
`Not Available`. That happens on AMD too, so it is not an NVIDIA issue;
another worker owns it.

### The host-side workaround, kept as documentation

Before the guest fix existed, a host-side workaround in virglrenderer proved
the diagnosis: `patches/host/virglrenderer/workaround/0002-*.patch`. When the
driver refuses the feedback-loop bit, `vkCreateImage` and both image-format
queries retry without it. With that render server alone, dwm survived, the
user logged on, and D3D9, D3D11 and D3D12 passed. The image-format part then
fixed Vulkan and OpenGL too (`02-render-server-patch/`, and `03-host-patches/`
with the QEMU fix). It is no longer needed. `build-virglrenderer.sh` applies
it only with `WITH_WORKAROUND=1`.

## Fault 2: QEMU's egl-headless cannot show a LINEAR dma-buf on NVIDIA

Even with the desktop running, the console stayed black
(`02-render-server-patch/console-black.png`). A guest-side `CopyFromScreen`
of the same moment shows the desktop (`guest-copyfromscreen-desktop.png`).

Yttrium scans out through `SET_SCANOUT_BLOB`: a LINEAR B8G8R8A8 image in
exported Venus memory. QEMU's `egl_dmabuf_import_texture()` imports the
dma-buf with no modifier (`DRM_FORMAT_MOD_INVALID`) and binds it to
`GL_TEXTURE_2D`. egl-headless then attaches that texture to a framebuffer
and blits it for VNC. NVIDIA's EGL lists LINEAR (`0x0`) for `XRGB8888` as
**external only**, so the `GL_TEXTURE_2D` bind fails with
`GL_INVALID_OPERATION`. The texture stays empty, and nothing reports an error.
A host test program shows this for both a GLES and a desktop core context
(`host-tests/dmabuftest.c`, `dmabuftest-nvidia-gles.txt`,
`dmabuftest-nvidia-core.txt`):

```
XRGB8888 modifier 0x0000000000000000 external_only 1
INVALID: TEXTURE_2D target glerr 0x502
INVALID: TEXTURE_EXTERNAL_OES target glerr 0x0
```

RADV allows LINEAR in `GL_TEXTURE_2D`, which is why the AMD console works.
The same fault explains `docs/host.md`'s `no-rn-amd` black console. The host
probe blamed that on the GPU mismatch, but egl-headless on NVIDIA would have
been black either way.

### The fix: `patches/host/qemu/0001`

The patch changes QEMU 11.1.1 in two places:

- `egl_dmabuf_import_texture()`: if the `GL_TEXTURE_2D` import fails, import
  the image as `GL_TEXTURE_EXTERNAL_OES` and record the texture as external.
- egl-headless: an external scanout gets its own `GL_TEXTURE_2D` behind
  `guest_fb`. Before each read-back, a `samplerExternalOES` shader draws the
  dma-buf into it at 1:1, and the existing blit and cursor paths then run
  unchanged. The vertex array object is created per draw. QEMU's current GL
  context changes when the guest reboots (virgl reset), and vertex array
  objects are not shared between contexts. A stored one left the console
  black after every guest reboot
  (`host-tests/qemu-egl-debug-reboot-excerpt.txt`).

`patches/host/build-qemu.sh` builds it, natively rather than in Docker. QEMU
links glib, epoxy, gbm and virglrenderer, and a current Arch container is
already newer than the frozen host (glib 2.90 against 2.88). The build only
reads the host's libraries and installs nothing. vmlab finds QEMU on `PATH`,
so `labs/nvidia/vmlab-nv.sh` puts `out/host/qemu/bin` first on the isolated
supervisor's `PATH` (`WIN3D_QEMU_DIR`).

## How to run it

```sh
build/mesa/build.sh && scripts/build.sh --package-only  # package with patches/mesa/0020
patches/host/build-qemu.sh                              # out/host/qemu/bin/qemu-system-x86_64
WIN3D_QEMU_DIR=$PWD/out/host/qemu/bin labs/nvidia/vmlab-nv.sh up
VMLAB=$PWD/labs/nvidia/vmlab-nv.sh scripts/install-into-vm.sh .scratch/labs/nvidia-a win11
labs/nvidia/vmlab-nv.sh vm screenshot win11 shot.png      # vmlab >= 0.24 captures GL consoles over VNC
labs/nvidia/vmlab-nv.sh down && labs/nvidia/vmlab-nv.sh --stop-supervisor
```

`vmlab-nv.sh` copies `labs/nvidia/vmlab.wcl` into the lab directory once.
It refuses a copy made before vmlab 0.25, which still carries
`gpu.address`; copy the current file over it.

`vmlab-nv.sh` also accepts:

- `WIN3D_RENDER_SERVER`: a render server path, passed to QEMU as
  `RENDER_SERVER_EXEC_PATH`.
- `WIN3D_QEMU_ENV`: debug variables for QEMU's environment, such as
  `VKR_DEBUG=validate`, `VKR_LOG_IMAGES=1` or `VK_ADD_LAYER_PATH`.

Both take effect only when the isolated supervisor starts. The validation
layer used here came from Arch's `vulkan-validation-layers 1.4.363.0-1`
package, unpacked under `.scratch/` (sha256
`904f4eba2e708c9c96579900688a284309cd56dc09b5af3f78c563ffda13df9e`).

## What was tried

| Step | Outcome |
| --- | --- |
| Diagnostic render server (`patches/host/virglrenderer/0001`), selected with `RENDER_SERVER_EXEC_PATH` | Named the failing `vkCreateImage` and its create-info |
| Native host test program | Reproduced the refusal without a VM; RADV accepts |
| Validation layer on the render server (`VKR_DEBUG=validate`) | Confirmed the refusal; the remaining messages (vertex attribute type mismatches, dma-buf memory bound to images without external info, `SPV_EXT_shader_viewport_index_layer`) are not fatal on either GPU |
| Host workaround (`workaround/0002`), first version | Crashed the render server: `vkr_image_create_and_add` replaces `args->device`, so the retry used a driver handle. Fixed by restoring the handle |
| Host workaround, final | Desktop, D3D9, D3D11 and D3D12 passed; Vulkan and OpenGL needed the image-format part too |
| Console: GL texture import test on the host | Found the external-only LINEAR import |
| QEMU patch v1 (shared VAO) | Worked from a cold boot, black after a guest reboot; v2 creates the VAO per draw |
| Guest fix `patches/mesa/0020`, stock render server | Everything passes; the host workaround is not needed |

## Limits and open items

- **Both RTX 5090s are visible to the guest.** `VK_LOADER_DRIVERS_SELECT=*nvidia*`
  matches both, and the loader's vendor and device filters cannot tell two
  identical cards apart. vulkaninfo lists two Venus devices with the same
  UUID (`04-guest-fix/vulkaninfo-summary.txt`). Applications pick GPU0, and
  `nvidia-smi` showed every render worker on `0000:01:00.0`, the GPU that
  the lab named (`gpu.address` on vmlab 0.24; `host_gpu = "Nvidia1"` on
  0.25). A guest that picks GPU1 would render on
  `0000:05:00.0` while egl-headless reads from `0000:01:00.0`. This is a
  vmlab item (below).
- **vmlab cannot use the patched QEMU itself.** The lab needs its own
  supervisor whose `PATH` puts the patched QEMU first. The draft request is in
  `.scratch/workers/nvidia/vmlab-handoff.md` in the main checkout.
- **The patched QEMU covers egl-headless only.** Other GL displays (gtk,
  dbus, spice) still bind external-only dma-bufs as `GL_TEXTURE_2D`. A cursor
  dma-buf that is external-only would not show either. The Yttrium cursor did
  not need it here.
- **Upstream.** Fault 1 belongs in `arehnman/virtio-win-mesa` (the
  venus2 winsys). Fault 2 belongs in QEMU upstream. That fix needs the
  external-sampler copy for every GL display, or an import that passes the
  real modifier, so drivers can pick a non-external path.
- **vulkaninfo on this branch.** The package's own `vulkaninfo.exe` crashed
  creating its window (`0xc0000005` at `+0x1a97`). `main` has the fix
  (95efcdf, user32 loading). The evidence here uses `main`'s build of it,
  from `build/vulkan-sdk` at `main`, copied to `C:\win3d\vk\`.

## Evidence index (`docs/evidence/nvidia/`)

| Directory | Configuration | What it shows |
| --- | --- | --- |
| `01-repro/` | host QEMU, diagnostic render server, package without 0020 | dwm crash, black console, the failing `vkCreateImage` |
| `02-render-server-patch/` | host QEMU, render server with the workaround | desktop alive in the guest, console black; probe tables with and without the image-format part |
| `03-host-patches/` | patched QEMU, render server with the workaround | all probes pass, console screenshots, vulkaninfo, dxdiag |
| `04-guest-fix/` | patched QEMU, **stock** render server, package with 0020 | all probes pass, console screenshots (first boot and after a reboot), vulkaninfo, dxdiag, QEMU argv and environment |
| `05-amd-regression/` | plain vmlab on the AMD iGPU, package with 0020 | no regression: all probes pass, console screenshots |
| `host-tests/` | host only | `imgtest.c`, `dmabuftest.c` and their outputs; the QEMU reboot debug excerpt |
