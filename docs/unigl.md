# Unigine Heaven and Valley with OpenGL on NVIDIA (compat finding F4)

The compatibility sweep ([compat.md](compat.md), F4) found that Unigine
Heaven 4.0 and Valley 1.0 crash under OpenGL on an RTX 5090 (`Nvidia2`) and
pass on the AMD iGPU (`AMD1`). Heaven took an access violation in
`viogpu_wgl.dll`+`0x2699c8`. Valley failed fast with `0xC0000409`. The render
server logged `vkBindBufferMemory2 resulted in CS error` after
`failed to look up object N of type 8` (type 8 is `VkDeviceMemory`).

The cause is the RTX 5090's CPU-visible VRAM window and two guest-side faults
that turn running out of it into a crash. The fix is three Mesa patches:
nvva's `0070` and this page's `0085` and `0086`. With them, both
benchmarks run the full time and exit cleanly on `Nvidia1` and `AMD1`.

The work was done on 2026-10-19 on helios: NVIDIA 610.57.04 on `Nvidia1`,
RADV (Mesa 26.2.2) on `AMD1`, QEMU 11.1.1 (the patched QEMU for NVIDIA),
virglrenderer 1.3.0 and vmlab 0.28.0-alpha. The evidence is in
`docs/evidence/unigl/`.

## Cause

Both RTX 5090s have no resizable BAR. Their BAR1 aperture is 256 MiB
(`nvidia-smi -q -d MEMORY`). The host driver exposes it as a third memory
heap, 246 MiB, with one memory type that is `DEVICE_LOCAL | HOST_VISIBLE |
HOST_COHERENT`. Venus passes it to the guest unchanged. Every process and
every VM on the card shares it, and worker nvva found that a Venus blob of it
costs twice its size there ([nvva.md](nvva.md)). On `AMD1` the visible
heap is the whole of VRAM, so none of what follows happens.

Zink puts `PIPE_USAGE_DEFAULT` and `DYNAMIC` buffers, and persistent and
coherent buffers, in that heap (`ZINK_HEAP_DEVICE_LOCAL_VISIBLE`). It maps
them directly for unsynchronized and plain write maps, with no staging
buffer. Heaven and Valley make many such buffers. Three things then go
wrong, one after another.

1. **The host's out-of-memory error is lost.** Venus submits
   `vkAllocateMemory` asynchronously and reports `VK_SUCCESS`. When the host
   allocation fails, the guest never learns of it. Zink has a fallback for
   exactly this case: it retries in another heap on
   `VK_ERROR_OUT_OF_DEVICE_MEMORY`, but the fallback never runs. The next
   command that names the memory fails to decode on the host
   (`failed to look up object N of type 8`). That is fatal to the whole Venus
   context, and the application crashes soon after. This is the F4 log.
2. **Zink's fallback puts mapped buffers in memory that cannot be mapped.**
   When an allocation from the visible heap fails, `allocate_bo()` retries in
   `ZINK_HEAP_DEVICE_LOCAL` unless the buffer is coherent or dynamic. A
   `DEFAULT` buffer then lives in memory that is not host-visible, and
   `zink_buffer_map` still maps it directly. With nvva's `0070` the same
   happens without any failure: with no visible-VRAM type left, the screen
   fills that heap with the `DEVICE_LOCAL` types. On Venus, `vkMapMemory` of
   such memory asks the host for a mappable blob of memory that the host did
   not allocate exportable. The render server logs `vkr: mem is not
   exportable`, QEMU logs `ctrl 0x10c, error 0x1200`, the map fails, and
   Heaven takes the same access violation in `viogpu_wgl.dll`.
3. **An allocation that succeeds can still fail to map.** When the host
   allocates from the visible heap but BAR1 has no room for QEMU's mapping of
   the blob, `RESOURCE_MAP_BLOB` fails (`ctrl 0x208, error 0x1200`), and the
   host kernel logs `NVRM: dmaAllocMapping_GM107: can't alloc VA space for
   mapping`. No guest-side check can see this one coming. nvva's `0070`
   removes it by keeping guest allocations out of the visible heap.

The guest-side Venus log in `docs/evidence/unigl/3-instrumented/` shows
fault 2 directly. With every allocation made synchronously (instrumentation
`src/venus-alloc-log.diff`), all of Heaven's allocations succeed. The one
failure is the map of a 2 MiB buffer in memory type 1, which is
device-local only (`venus-alloc-0070-heaven.txt`).

## Fix

| Patch | Owner | Change |
| --- | --- | --- |
| `patches/mesa/0070` | nvva | The Venus ICD reports a device-local, host-visible type whose heap is less than half the size of the largest device-local heap as device-local only. Guest allocations stay out of BAR1 (fault 3) |
| `patches/mesa/0085` | unigl | `vkAllocateMemory` from a device-local heap smaller than the largest device-local heap is synchronous, so the caller sees the host's result and Zink's fallback runs (fault 1) |
| `patches/mesa/0086` | unigl | Zink keeps those buffers mappable. A missing visible-VRAM heap is filled with the `HOST_VISIBLE_COHERENT` types, and a failed visible-VRAM allocation is retried there whenever the resource's `memoryTypeBits` allow it. Images that cannot use those types still go to `DEVICE_LOCAL` (fault 2) |

`0070` is nvva's patch, carried here byte for byte (nvva `e24e356`) so that
the proof package is the series that merges. nvva carries `0086` the same way,
so `0070` never lands without it. nvva's draft `0071` (Yttrium display images
in system memory) is not part of the series. In this worker's runs it made
dwm lose its Venus context every few seconds from boot on `Nvidia1`. nvva
traced that to NVIDIA refusing cross-process imports of system-memory image
dma-bufs, and dropped `0071`.

With `0070` in place, `0085` is reached only if a driver allocates the
small heap's type as device-local memory after its other device-local types
fail. It stays in the series because without it any such failure is fatal to
the context. `0086` alone has no effect on a device with a visible-VRAM type
until an allocation from it fails.

## Evidence

Each run is `docs/evidence/unigl/src/run-compat.sh` (the compat sweep's
`tests/apps/compat/run.sh` under this worker's lab and evidence names), with
`--only heaven-opengl,valley-opengl` and the default 150 s. The runs are in
order, and later runs reused the same lab. `bar1.txt` is the host's BAR1 use
sampled every 2 s (`src/bar1-sample.sh`). The run directories are named after
the Mesa patches in the package.

| Run | Package | GPU | Heaven | Valley |
| --- | --- | --- | --- | --- |
| `1-stock` | main at `5781598` | `Nvidia1`, BAR1 246 to 250 MiB of 256 | `0xC0000409` at 122 s | `0xC0000409` at 48 s |
| `2-nvva-0070-0071-0085` | + nvva drafts, first `0085` | `Nvidia1` | `0xC0000005` in `viogpu_wgl.dll` at 82 s (`mem is not exportable`) | the same at 38 s |
| `3-instrumented` | + allocation log, first `0086` | `Nvidia1` | runs; dwm dead (`0071`), close times out | the same |
| `4-nvva-0070-0071-0085-0086v1` | nvva drafts, `0085`, first `0086` | `AMD1` | clean, 23.9 fps | clean, 18.2 fps |
| `4-nvva-0070-0071-0085-0086v1` | the same | `Nvidia1`, fresh lab | runs; dwm dead (`0071`), close times out | the same |
| `5-0085-only` | `0085` | `Nvidia1` | `0xC0000005` at 36 s (`mem is not exportable`: fault 2 through Zink's fallback) | clean, 61.2 fps |
| `6-0085-0086` | `0085`, `0086` | `Nvidia1` | `0xC0000005` at 31 s (`ctrl 0x208`: fault 3) | clean, 34.1 fps |
| `7-final` | `0070`, `0085`, `0086` | `Nvidia1` | clean, 71.9 fps | clean, 63.9 fps |
| `7-final` | the same, built at `3bf3241` | `AMD1`, fresh lab | clean, 17.2 fps | clean, 20.7 fps |

The frame rates are the means of the on-screen counter, read with OCR from
the console frames (`fps.txt`; [compat.md](compat.md) explains the method).
`7-final` on `Nvidia1` logged no render-server errors and no NVIDIA Xid.
The console frames (`console-1.png` to `console-3.png`) show both benchmarks
rendering.

Before `7-final`, no package without `0071` and with `0070` existed. The
`3-instrumented` and `4` runs on `Nvidia1` show what `0071` did to dwm, not
F4. The same lab, rebooted with the stock package or with `0085` only, logged
no lost Venus contexts in the five minutes after boot.

The `7-final` runs on `Nvidia1` used a package built before `0070` was
committed here. Its Mesa DLLs are byte-identical to those of the package
built at `3bf3241`, which the `AMD1` run and the acceptance runs used.

`tests/e2e/run.sh --template windows-11` with the package built at
`3bf3241` (nvva's `0070`, `0085`, `0086` on top of main's series) passed 12
of 12 on both GPUs, with no NVIDIA Xid during the `Nvidia1` run. The
evidence is copied to `docs/evidence/unigl/e2e/amd1/` and
`docs/evidence/unigl/e2e/nvidia1/` (`results.txt`).

### After merging main

nvva's `0070` and `0086` merged to main (`b74ea3c`), followed by unid3d's
`0090`. This branch merged main, and the package built at `d3ecec3` (main's
series plus `0085`) was checked again:

| Check | `AMD1` | NVIDIA |
| --- | --- | --- |
| `tests/e2e/run.sh --template windows-11` | 12 of 12 (`e2e-merged/amd1/`) | 12 of 12 on `Nvidia2`, BAR1 peak 145 MiB, no Xid (`e2e-merged/nvidia2/`) |
| Heaven and Valley OpenGL (`8-merged/`) | clean, 13.4 and 20.2 fps | clean on `Nvidia2`, 52.7 and 59.9 fps; BAR1 peak 152 MiB, no render-server errors, no Xid |

A first `Nvidia2` e2e run gave 11 of 12. The D3D12 x86 probe passed, but no
console capture showed its window (`e2e-merged/nvidia2-run1/`). A first
`Nvidia2` OpenGL run failed: Heaven's DXGI swapchain (the Yttrium D3D11 UMD,
which Mesa's WGL presents through) could not allocate a display image, and
its ring timed out. Valley crashed in `viogpu_wgl.dll`. During both runs
three of this project's VMs shared `Nvidia2`, over the limit of two, and
BAR1 stood at 238 MiB of 256 with the host logging "can't alloc VA space".
Its evidence was discarded, and both runs were repeated within the limit.

## What remains

- Fault 3 is not fixed in general. A guest whose driver still allocates
  from the visible heap (for example a native Vulkan application that asks
  for `DEVICE_LOCAL | HOST_VISIBLE`) can still fail to map when BAR1 is full.
  With `0070` the Venus ICD no longer offers such a type, so Zink, DXVK
  and vkd3d-proton stay out of BAR1. The Yttrium D3D UMD queries memory
  types itself and still puts display images there (nvva dropped `0071`,
  which moved them, because NVIDIA cannot import system-memory image
  dma-bufs across processes). Mesa's WGL presents through that UMD, so an
  OpenGL program can still lose its swapchain when BAR1 runs out. That
  happened with three VMs on one card (above). nvva's page has the BAR1
  accounting.
- Zink's direct maps of buffers that are not host-visible are now
  unreachable in this package, but `zink_buffer_map` itself still maps them
  without a staging buffer. A device that lacks `HOST_VISIBLE_COHERENT`
  memory for a buffer would still hit it.
- The Godot (F1) and GZDoom (F2) Vulkan failures on NVIDIA show the same
  render-server pattern. They belong to other workers and were not run
  here.
