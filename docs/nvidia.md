# Venus on the NVIDIA RTX 5090

This page explains why the package failed on this host's RTX 5090s and what
fixes it. It also records how the failure was reproduced, what was tried, and
who owns each fix. The diagnosis cites files under `docs/evidence/nvidia/`;
the acceptance runs are under `docs/evidence/acceptance/windows-11-nvidia1/`
and `windows-11-nvidia2/`. Diagnosis: 2026-10-17. Acceptance on vmlab
0.27.0-alpha (`Nvidia2`: 0.28.0-alpha): 2026-10-18.

## Result

It works, with plain vmlab 0.27.0-alpha and the user's normal supervisor.
`tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1` passes 12 of
12 Done checks on the first RTX 5090 (`0000:01:00.0`, `renderD128`), whose
host config block names the patched QEMU as `qemu_binary`:

- the install, `vulkaninfo` (exactly one device, `Virtio-GPU Venus (NVIDIA
  GeForce RTX 5090)`), dxdiag hardware acceleration, and all ten probes
  (Vulkan, D3D9, D3D11, D3D12 and OpenGL, x64 and x86), each found in the
  console captures;
- `qemu-argv-env.txt`: `vmlab: argv:` starts with the `qemu_binary` and
  `-L /usr/share/qemu`, `vmlab: env:` has `VMLAB_VK_PCI_ADDRESS=0000:01:00.0`,
  and the render server logged
  `vmlab-pci-filter: 0000:01:00.0: kept NVIDIA GeForce RTX 5090, hid 1`;
- `status-v.txt`: `host_gpu=Nvidia1 qemu=<path>`;
- `nvidia-smi-during-probes.txt`: every `virgl_render_server` on
  `0000:01:00.0`.

`--template windows-10 --host-gpu Nvidia1` also passes 12 of 12
(`docs/evidence/acceptance/windows-10-nvidia1/`).

The DirectX SDK's D3D9 `BasicHLSL` sample, a stretch application, hangs the
guest on NVIDIA with a host Xid 69 ([known limits](known-limits.md#host-gpu-support)).

The same run on the second RTX 5090, `--host-gpu Nvidia2`
(`0000:05:00.0`, `renderD130`), also passes 12 of 12 on vmlab 0.28.0-alpha
now that helios's host config names the same patched QEMU as `Nvidia2`'s
`qemu_binary` (`docs/evidence/acceptance/windows-11-nvidia2/`, 2026-10-18,
commit `99ad0c1`): one Venus device, every probe on the console,
`vmlab: argv:` starting with the `qemu_binary`, `VMLAB_VK_PCI_ADDRESS=0000:05:00.0`,
the filter line `vmlab-pci-filter: 0000:05:00.0: kept NVIDIA GeForce RTX 5090, hid 1`,
and `status -v`: `host_gpu=Nvidia2 qemu=<path>`. This run has no
`nvidia-smi` capture; the earlier stock-QEMU run on `Nvidia2` (black
console, 4 of 12; git history at `a58acd3`) had every render server on
`0000:05:00.0`.

Two separate faults stopped it. Neither is a bug in the NVIDIA driver.

| # | Symptom | Failing call | Fix | Owner |
| --- | --- | --- | --- | --- |
| 1 | dwm crashes before logon (`0x889800c0`); Vulkan and OpenGL presents fail | `vkCreateImage` and `vkGetPhysicalDeviceImageFormatProperties2` return `VK_ERROR_FORMAT_NOT_SUPPORTED` for LINEAR images with `VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT` | `patches/mesa/0020-yttrium-gate-feedback-loop-usage-on-format-support.patch` (guest, in the package) | Mesa: the Yttrium venus2 winsys (`arehnman/virtio-win-mesa`) |
| 2 | Black vmlab console, although the guest renders | QEMU egl-headless: `glEGLImageTargetTexture2DOES(GL_TEXTURE_2D)` gives `GL_INVALID_OPERATION` for the LINEAR scanout dma-buf | The vmlab QEMU: upstream QEMU master plus the series on [VMLabDev/qemu](https://github.com/VMLabDev/qemu/tree/vmlab/egl-headless-external-dmabuf) `vmlab/egl-headless-external-dmabuf`, pinned in `patches/host/PINS` (a host build) | QEMU upstream (`ui/egl-helpers.c`, `ui/egl-headless.c`); vmlab must be able to run that QEMU |

Fix 1 is in the package. Fix 2 is a host component: a QEMU binary that
vmlab (0.26.0-alpha or later) runs for every VM on that host GPU, because
the host config names it as the GPU's `qemu_binary` ("Host setup" below).
Nothing is installed system-wide (decision D3).

## Reproduction

The host stack is frozen: QEMU 11.1.1, virglrenderer 1.3.0, Mesa 26.2.2, and
NVIDIA 610.57.04. The diagnosis runs recorded here used vmlab 0.24.0-alpha,
with the lab naming the GPU as
`gpu { mode = "vulkan" address = "0000:01:00.0" }` and an isolated vmlab
supervisor that put the patched QEMU first on `PATH`
(`04-guest-fix/qemu-argv-env.txt` records what vmlab gave QEMU). The steps
below are for vmlab 0.27, where none of that is needed.

1. Build the package with `scripts/build.sh`. A bundle built before
   `patches/mesa/0020` reproduces fault 1.
2. Use the lab `labs/nvidia/vmlab.wcl`: `secure_boot = false` and
   `gpu { mode = "vulkan" host_gpu = "Nvidia1" }`. The host config,
   `~/.config/vmlab/config.wcl`, declares
   `gpu "Nvidia1" { address = "0000:01:00.0" }`. vmlab then passes
   `-display egl-headless,rendernode=/dev/dri/renderD128` and puts
   `VK_LOADER_DRIVERS_SELECT=*nvidia*` in QEMU's environment. Leave out
   the GPU's `qemu_binary` to reproduce fault 2.
3. Copy it into a lab directory and run `vmlab up` there.
4. Install the bundle with `scripts/install-into-vm.sh <lab-dir> win11`.
   Then set up the test desktop with `tests/guest/Set-TestDesktop.ps1 -Reboot`.
   `tests/e2e/run.sh --host-gpu Nvidia1` does steps 1 to 4 and the checks.

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
the diagnosis: `patches/host/virglrenderer/workaround/0003-*.patch` (numbered 0002 until the render-server fix of [`n5.md`](n5.md) took that number). When the
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

### The fix: the VMLabDev/qemu branch

The fix lives on the fork
[VMLabDev/qemu](https://github.com/VMLabDev/qemu/tree/vmlab/egl-headless-external-dmabuf),
branch `vmlab/egl-headless-external-dmabuf`, as a six-commit series on
upstream QEMU master `a19f59a72d` (`v11.1.0-2513-ga19f59a72d`, version
11.1.50), prepared for upstream review. `patches/host/PINS` pins the commit
this repository builds, `d8a6048aae` as of 2026-10-18:

```
d8a6048aae ui/sdl2: show external-only dma-buf scanouts
f95d311446 ui/gtk: show external-only dma-buf scanouts
40d3c41a0a ui/egl-headless: show external-only dma-buf scanouts
dd46c88676 ui/egl-helpers: let an egl_fb wrap an external texture
674931d277 ui/shader: add a texture blit program for external textures
acb3659eea ui/egl-helpers: import external-only dma-bufs as external textures
```

Master already has `baca25172d` ("ui/egl: fix render node cleanup order"),
so this build also fixes the shutdown crash in
[known limits](known-limits.md#qemu-crashes-on-shutdown). Each of the six
commits builds on its own, with the egl-headless, GTK and SDL displays
enabled. Before master, the series sat on tag `v11.1.1` (`78bc99a513`, the
source of 0.1.0-alpha, kept as tag `vmlab-win3d-0.1.0-alpha` on the fork),
then briefly on v11.1.2 with a backport of `baca2517` (`672843fce5`).

Until 2026-10-18 the fix was one patch, `patches/host/qemu/0001` (fork
commit `f5832b34dd`), which only changed egl-headless; the series replaced
it and also covers the GTK and SDL displays. The evidence in this page and in
[`acceptance.md`](acceptance.md) before that date used the single patch. As of
2026-10-18, neither QEMU master nor v11.1.2 handles external-only dma-bufs
(`egl_dmabuf_import_texture()` binds only `GL_TEXTURE_2D`), and no upstream
issue reports it. Upstream submission still needs a `Signed-off-by:` from the
author.


The single patch changed QEMU 11.1.1 in two places, and the series keeps the
same approach, with the shader and the external `egl_fb` moved into the
shared `ui/shader.c` and `ui/egl-helpers.c` so GTK and SDL can use them:

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

`patches/host/build-qemu.sh` fetches exactly the pinned commit and builds
it, natively rather than in Docker. `patches/host/bump-qemu.sh` moves the pin
to the branch tip and prints the commits between; builds never follow the
branch by themselves. QEMU
links glib, epoxy, gbm and virglrenderer, and a current Arch container is
already newer than the frozen host (glib 2.90 against 2.88). The build only
reads the host's libraries and installs nothing.

## Host setup (vmlab 0.27.0-alpha or later)

### The patched QEMU on the GPU's profile

vmlab 0.26.0-alpha added two optional attributes to a host GPU block in
`~/.config/vmlab/config.wcl` (vmlab handoff `20261018-014452`):

- `qemu_binary`: an absolute path. Every VM whose `gpu {}` resolves to that
  host GPU runs it instead of `qemu-system-x86_64` from `PATH`. vmlab adds
  `-L /usr/share/qemu` (the stock QEMU's data directory), so a build without
  its own firmware and ROMs still starts. `vmlab validate` refuses a path
  that is relative, missing or not executable.
- `qemu_env`: a record of extra variables for QEMU's environment, applied
  after the ones vmlab derives, which it overrides. Keys are identifiers
  joined by `:`, not `=`: `qemu_env = { VKR_DEBUG: "validate" }`.

Give every NVIDIA GPU that a Venus lab may use a `qemu_binary`. For
helios that is the following, and since 2026-10-18 the real config gives
both `Nvidia1` and `Nvidia2` one:

```wcl
import <vmlab-host.wcl>

gpu "AMD1"    { address = "0000:72:00.0" }                  // AMD iGPU (RADV, renderD129)
gpu "Nvidia1" {                                             // RTX 5090 (renderD128)
  address     = "0000:01:00.0"
  default     = true
  qemu_binary = "/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
}
gpu "Nvidia2" {                                             // RTX 5090 (renderD130)
  address     = "0000:05:00.0"
  qemu_binary = "/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
}
```

The lab file stays plain: `gpu { mode = "vulkan" host_gpu = "Nvidia1" }`,
as in `labs/nvidia/vmlab.wcl` and `labs/acceptance/vmlab.wcl` with
`tests/e2e/run.sh --host-gpu Nvidia1`. To check what a running VM got:

- `vmlab status -v` shows `host_gpu=Nvidia1 qemu=<path>`.
- `qemu.log` in the VM's state directory starts with
  `vmlab: argv: <qemu_binary> ... -L /usr/share/qemu ...` and a
  `vmlab: env:` line.

### Getting the patched QEMU

Either of these gives the same binary, the fork commit `patches/host/PINS`
pins (`--version` prints `QEMU emulator version 11.1.50 (vmlab <commit>)`):

- The release asset `qemu-vmlab-egl-<version>-x86_64-linux.tar.zst`
  ([`release.md`](release.md)). Its `install.sh` checks the host's libraries
  with `ldd`, installs into `~/.local/opt/qemu-vmlab-egl` (or a given
  prefix) and prints the `qemu_binary` line. It suits hosts with the same
  libraries as the build host (Arch or CachyOS).
- `patches/host/build-qemu.sh` builds the pinned fork commit against this
  host's libraries and writes `out/host/qemu/bin/qemu-system-x86_64`. Point
  `qemu_binary` at its absolute path, or copy the `out/host/qemu/` tree
  somewhere stable first.

helios used `/home/wil/.local/opt/qemu-11.1.1-vmlab-egl/` for the 11.1.1
builds, the name 0.1.0-alpha's `install.sh` chose; later builds default to
`/home/wil/.local/opt/qemu-vmlab-egl/`. Either way, the
binary links the host's own glib, epoxy, gbm and virglrenderer, so rebuild it
when those packages change.

### The patched render server on the GPU's profile

Each NVIDIA GPU block also names the vmlab render server, through
`qemu_env`:

```wcl
gpu "Nvidia1" {
  address     = "0000:01:00.0"
  default     = true
  qemu_binary = "/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
  qemu_env    = { RENDER_SERVER_EXEC_PATH: "/home/wil/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server" }
}
```

Without it, every Venus fence on NVIDIA takes at least 10 ms, and windowed
D3D9 runs at about 20 to 40 frames per second instead of the refresh rate.
[`n5.md`](n5.md) has the cause: virglrenderer creates the fences its queue
threads wait on as exportable SYNC_FD fences, and NVIDIA 610.57 wakes
`vkWaitForFences` on those up to 10 ms late.
[`patches/host/virglrenderer/0002`](../patches/host/virglrenderer/0002-vkr-do-not-create-queue-sync-fences-exportable.patch)
creates them plain.

The server is virglrenderer 1.3.0's `virgl_render_server` with
`patches/host/virglrenderer/0001` and `0002`. Only the server is replaced.
QEMU keeps the host's own `libvirglrenderer` 1.3.0, which forks the
program named by `RENDER_SERVER_EXEC_PATH` instead of
`/usr/lib/virgl_render_server`. Venus runs entirely in the server, and the
socket protocol between the two is the one of the same release, 1.3.0. A
host whose virglrenderer package moves to another version needs the server
rebuilt from that version.

To get it, either:

- install the release asset `virglrenderer-vmlab-<version>-x86_64-linux.tar.zst`
  ([`release.md`](release.md#install-the-render-server-for-nvidia-hosts)),
  whose `install.sh` checks the host and prints the `qemu_env` line; or
- run `patches/host/build-virglrenderer.sh --install`, which builds it
  natively for the x86-64-v2 baseline against this host's libraries and
  installs it into `~/.local/opt/virglrenderer-vmlab`.

To check what a VM got:

- the `vmlab: env:` line at the top of the VM's `qemu.log` ends with
  `RENDER_SERVER_EXEC_PATH=<path>`;
- `ps -C virgl_render_server -o pid,args` (or `pgrep -af virgl_render_server`)
  lists `<prefix>/libexec/virgl_render_server --socket-fd N` for the VM,
  not `/usr/lib/virgl_render_server`.

Helios's host config has given `Nvidia1` and `Nvidia2` this `qemu_env`
line since 2026-10-19 (backup `config.wcl.bak-hostvirgl-20261019-113229`);
a fresh `Nvidia2` VM then ran the server
(`docs/evidence/hostvirgl/deploy/`).

`vmlab status -v` (0.28.0-alpha) names the `host_gpu` and `qemu_binary` but
not `qemu_env`; a vmlab request to show it was drafted.

`AMD1` does not use it. RADV wakes exportable and plain fences alike
([`n5.md`](n5.md#cause)), and the A/B in
[`docs/evidence/hostvirgl/`](evidence/hostvirgl/README.md) found no
difference on AMD.

### One GPU per guest: the PCI-filter layer

`VK_LOADER_DRIVERS_SELECT=*nvidia*` matches both RTX 5090s on helios, and
before vmlab 0.27 the guest saw two Venus devices with the same UUID.
vmlab 0.27.0-alpha ships an implicit Vulkan layer,
`VK_LAYER_VMLAB_pci_filter` (vmlab handoff `20261018-092228`). It is built
into the `vmlab` binary and needs no install or setting. Every `vulkan` VM
whose host GPU resolves gets it, through these variables in QEMU's
environment:

```
VK_LOADER_DRIVERS_SELECT=*nvidia*
VK_ADD_IMPLICIT_LAYER_PATH=/home/wil/.local/share/vmlab/vk-layer/<digest>
VMLAB_VK_PCI_ADDRESS=0000:01:00.0
```

The layer answers `vkEnumeratePhysicalDevices` with only the device whose
`VK_EXT_pci_bus_info` address matches. The render server logs what it kept
in `qemu.log`, once per Venus context:

```
vmlab-pci-filter: 0000:01:00.0: kept NVIDIA GeForce RTX 5090, hid 1
```

`VMLAB_VK_PCI_FILTER_DISABLE=1` in the GPU's `qemu_env` turns it off.

### Debug variables

`qemu_env` replaces the environment variables the old isolated-supervisor
wrapper took:

| Purpose | `qemu_env` entry |
| --- | --- |
| Another render server, for example one built with `WITH_WORKAROUND=1` or with extra logging | `RENDER_SERVER_EXEC_PATH: "/abs/path/virgl_render_server"` (replaces the vmlab render server above) |
| The Vulkan validation layer on the render server | `VKR_DEBUG: "validate"`, plus `VK_ADD_LAYER_PATH` for a layer outside the system path |
| Log every image the render server creates | `VKR_LOG_IMAGES: "1"` (the vmlab render server has the logging patch, 0001) |

vmlab reads the host config when a VM starts, so stop the VM after changing
it. The validation layer used in the diagnosis came from Arch's
`vulkan-validation-layers 1.4.363.0-1` package, unpacked under `.scratch/`
(sha256
`904f4eba2e708c9c96579900688a284309cd56dc09b5af3f78c563ffda13df9e`).

## What was tried

| Step | Outcome |
| --- | --- |
| Diagnostic render server (`patches/host/virglrenderer/0001`), selected with `RENDER_SERVER_EXEC_PATH` | Named the failing `vkCreateImage` and its create-info |
| Native host test program | Reproduced the refusal without a VM; RADV accepts |
| Validation layer on the render server (`VKR_DEBUG=validate`) | Confirmed the refusal; the remaining messages (vertex attribute type mismatches, dma-buf memory bound to images without external info, `SPV_EXT_shader_viewport_index_layer`) are not fatal on either GPU |
| Host workaround (`workaround/0003`, then `0002`), first version | Crashed the render server: `vkr_image_create_and_add` replaces `args->device`, so the retry used a driver handle. Fixed by restoring the handle |
| Host workaround, final | Desktop, D3D9, D3D11 and D3D12 passed; Vulkan and OpenGL needed the image-format part too |
| Console: GL texture import test on the host | Found the external-only LINEAR import |
| QEMU patch v1 (shared VAO) | Worked from a cold boot, black after a guest reboot; v2 creates the VAO per draw |
| Guest fix `patches/mesa/0020`, stock render server | Everything passes; the host workaround is not needed |

## Limits and open items

- **BAR1 and the number of VMs per card.** Without Resizable BAR an
  RTX 5090's BAR1 is 256 MiB, shared by every VM on the card, and more than
  two Windows VMs per card run it out ([`nvva.md`](nvva.md)). Helios has
  Resizable BAR since 2026-10-19 (BAR1 32 GiB). With it, release candidate
  rc3 passed the acceptance on both cards, and three VMs on one card
  logged no `can't alloc VA space` line, with BAR1 at most 764 MiB
  ([rc3](evidence/rc3/README.md#rebar-load-check)).

- **Resolved in vmlab 0.27: both RTX 5090s were visible to the guest.**
  On vmlab 0.24 and 0.25, vulkaninfo listed two Venus devices with the same
  UUID (`04-guest-fix/vulkaninfo-summary.txt`). The PCI-filter layer above
  leaves one.
- **Resolved in vmlab 0.26: vmlab could not run the patched QEMU itself.**
  The diagnosis needed an isolated supervisor whose `PATH` put it first.
  The host GPU's `qemu_binary` replaces that.
- **NVIDIA's driver may still open a small context on the other card.**
  vmlab reports about 6 MiB on the hidden RTX 5090 inside
  `virgl_render_server` at instance creation; the layer cannot stop driver
  initialisation. `nvidia-smi` sampled during the acceptance probes listed
  every render server only on the kept card
  (`nvidia-smi-during-probes.txt` in each acceptance evidence directory).
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
