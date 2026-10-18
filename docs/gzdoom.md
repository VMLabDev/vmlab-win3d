# GZDoom: OpenGL context creation (F3) and Vulkan on NVIDIA (F2)

The compatibility sweep ([`compat.md`](compat.md)) found two GZDoom 4.14.2
failures:

- **F3:** with OpenGL, GZDoom stopped on both GPUs with "Unable to create an
  OpenGL render context. Insufficient driver support for context creation".
- **F2:** with Vulkan on `Nvidia2`, GZDoom showed a black window and failed
  fast with `0xC0000409` after 6 to 26 s, while the render server logged CS
  errors.

F3 was a bug in the WGL ICD, `viogpu_wgl.dll`, and is fixed by
`patches/mesa/0080`. F2 is the BAR1 exhaustion that worker `nvva` diagnosed
([`nvva.md`](nvva.md)), not a GZDoom-specific bug. `nvva`'s `0070` and
`0086` fix it, and GZDoom Vulkan now runs on NVIDIA. The work was done on
2026-10-19 on helios: RADV (Mesa 26.2.2) on `AMD1`, NVIDIA 610.57.04,
vmlab 0.28.0-alpha and virglrenderer 1.3.0. The evidence is in
`docs/evidence/gzdoom/`.

## Result

| Finding | Cause | Change | Status |
| --- | --- | --- | --- |
| F3, GZDoom OpenGL, both GPUs | `opengl32.dll` lost the pixel format of GZDoom's dummy window, so `wglCreateContext` failed with `ERROR_INVALID_PIXEL_FORMAT` (2000) before it reached the ICD | `0080`: the ICD pins `opengl32.dll` | Fixed. GZDoom renders and exits cleanly on `AMD1` and `Nvidia1`, at 35 fps |
| F2, GZDoom Vulkan, `Nvidia2` | The render-server lines are `nvva`'s "allocation refused" failure: the host refused a `vkAllocateMemory` on the 246 MiB BAR1 heap, which the guest had sent asynchronously | none here; `nvva`'s `0070` and `0086` | Fixed by those. GZDoom Vulkan runs clean on `Nvidia1`, at 35 fps, with no CS error in the render-server log |

## F3: why `wglCreateContext` failed

### What GZDoom does

GZDoom's Win32 OpenGL start-up (`src/common/platform/win32/win32glvideo.cpp`
at tag `g4.14.2`) runs in this order:

1. It creates a hidden 64×64 dummy window and calls GDI's
   `ChoosePixelFormat` (32-bit colour, 16-bit depth, no stencil) and
   `SetPixelFormat` on it.
2. Only then does it call `zd_wglCreateContext`. That function loads
   `opengl32.dll` with `LoadLibraryA("OpenGL32.DLL")` on first use
   (`src/common/rendering/gl_load/gl_load.c`). GZDoom does not import
   `opengl32.dll`.
3. It makes the context current and asks `wglGetProcAddress` for
   `wglChoosePixelFormatARB` and `wglCreateContextAttribsARB`.
4. If `wglChoosePixelFormatARB` is NULL, it stops with "Insufficient driver
   support for context creation". That is the only path to this message
   before a context exists.

### What went wrong

On Windows 11, when a process calls `ChoosePixelFormat` or `SetPixelFormat`
before it has loaded `opengl32.dll`, GDI loads `opengl32.dll` for that one
call and frees it afterwards. `opengl32.dll` loads the ICD and calls its
`DrvValidateVersion`, `DrvDescribePixelFormat` and `DrvSetPixelFormat` as
usual. The evidence for the temporary load:

- Inside `DrvValidateVersion` and `DrvSetPixelFormat`,
  `GetModuleHandleA("opengl32.dll")` returned a module (a temporary
  tracing build of the ICD).
- In the program, before and after each GDI call, the same call returned
  NULL, while `viogpu_wgl.dll` stayed loaded
  (`repro/amd1-before/gzgl-x64.txt`, the `modules` lines).

`opengl32.dll` keeps its own record of each window's pixel format. When GDI
freed it, the record of the dummy window went with it. When GZDoom then
loaded `opengl32.dll` itself:

- `GetPixelFormat` on the dummy window returned 0;
- `wglCreateContext` failed with `ERROR_INVALID_PIXEL_FORMAT` (2000) and
  never called `DrvCreateLayerContext`;
- `wglGetProcAddress` with no current context returned NULL.

`repro/amd1-before/gzgl-x64-diag.txt` shows all three. Setting the pixel
format again after `opengl32.dll` was loaded made `wglCreateContext` work.
GZDoom does not do that.

Programs that import `opengl32.dll`, such as the OpenGL probe, Blender and
Godot, never hit this: `opengl32.dll` is loaded before their first GDI
pixel-format call and stays loaded. An ICD that imports `opengl32.dll` keeps
it loaded too. `viogpu_wgl.dll` imports only GDI32, KERNEL32, USER32 and the
UCRT ([`build/mesa/README.md`](../build/mesa/README.md)).

The first diagnosis was an app-local `opengl32.dll` that logs the WGL calls
(`src/gltrace/`). With it in GZDoom's directory, GZDoom ran
(`repro/gltrace-gzdoom-amd1.txt`): the logging DLL loaded the real
`opengl32.dll` and never freed it. That pointed at the load order, and
`gzgl.c` then reproduced the failure exactly.

Two hypotheses were tested and ruled out:

- `DrvSetPixelFormat` refusing a second call for a window that already has
  a framebuffer. Making it accept the same format again did not help,
  because `opengl32.dll` never reached the ICD.
- The `OpenGLFlags` and `OpenGLVersion` registry values (3 and 4096). The
  repro failed the same way with flags 2 and with version 1, each after a
  reboot.

### The fix

`patches/mesa/0080-wgl-pin-opengl32-so-pixel-formats-set-through-gdi-last.patch`
changes `DrvValidateVersion`, the first entry point `opengl32.dll` calls in
an ICD. On the first call, it pins `opengl32.dll`
(`GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, ...)`). GDI's
`FreeLibrary` then no longer unloads it, so its pixel-format records last
for the life of the process.

Side effect: a process that calls a GDI pixel-format function keeps
`opengl32.dll` (and the ICD) loaded until it exits. Every OpenGL program
loads both anyway.

## Evidence

Two packages were used, both built by `scripts/build.sh` from this branch.
"Base" is `main` at `5781598` plus `0080` (manifest commit `4d879bb`).
"Merged" adds `main` at `b74ea3c`, which brought `nvva`'s `0070` and `0086`
(manifest commit `d0c6c3e`). Neither has the dropped `0071`.

| What | Where |
| --- | --- |
| Repro: GZDoom's start-up, call for call, with `opengl32.dll` loaded late as GZDoom loads it | `docs/evidence/gzdoom/src/gzgl.c`, built by `src/build.sh`, run by `src/run-repro.sh` (x64 and x86, through `tests/guest/Run-Probe.ps1`) |
| Repro on the 0.1.1 package at `5781598`: fails at step 1 (x64 and x86) | `repro/amd1-before/` |
| Repro with `0080` (its `viogpu_wgl.dll` copied over the installed one): passes all four steps, a 4.6 core context and the cleared frame read back (x64 and x86) | `repro/amd1-after/` |
| The WGL call trace of a GZDoom run with the logging `opengl32.dll` | `repro/gltrace-gzdoom-amd1.txt`, from `src/gltrace/` |
| GZDoom OpenGL and Vulkan through `tests/apps/compat/run.sh` on `AMD1`, fresh lab, base package: both clean at 35 fps | `amd1/` (`results.json`, `gzdoom-opengl/console-2.png`) |
| The same on `Nvidia1`, merged package: both clean, no Xid. The counter reads 35 fps, but the Start menu was open over the window in every frame, so the OCR found no counter. The render-server log of the run has no CS error | `nvidia1/` (`results.json`, `qemu-log.txt`, `host-nvrm.txt`) |
| `tests/e2e/run.sh --template windows-11` on `AMD1`, base package: 12 of 12 | `e2e-amd1/` (`results.txt`) |
| `tests/e2e/run.sh --template windows-11` on `Nvidia1`, merged package: 12 of 12 | `e2e-nvidia1/` (`results.txt`) |

`tests/apps/compat/run.sh` now takes `COMPAT_LAB` and `COMPAT_EV` from the
environment, so a worker can run it in a lab of its own name and keep the
sweep's evidence directory untouched:

```sh
COMPAT_LAB=win3d-gzdoom-amd1 COMPAT_EV=$PWD/docs/evidence/gzdoom/amd1 \
    tests/apps/compat/run.sh --host-gpu AMD1 --only gzdoom-opengl,gzdoom-vulkan
```

## F2: GZDoom Vulkan on NVIDIA

The F2 render-server lines (`docs/evidence/compat/nvidia2/gzdoom-vulkan/qemu-log.txt`):

```
virtio_gpu_virgl_process_cmd: ctrl 0x10c, error 0x1200
vkr: failed to look up object 76 of type 8
vkr: vkFreeMemory resulted in CS error
vkr: vkWaitRingSeqnoMESA resulted in CS error
```

Read in order:

1. Venus sends `vkAllocateMemory` without waiting for the result
   (`vn_device_memory_alloc_simple`, unless `VN_PERF=no_async_mem_alloc`).
   The host refused it, so the render server never created device memory
   76.
2. Command `0x10c` is `VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB`. The guest
   asked for a blob for memory 76 to map it, and the host answered
   `VIRTIO_GPU_RESP_ERR_UNSPEC`.
3. The guest freed the memory, and `vkFreeMemory` named an object the
   server did not have. That is a CS error, which kills the Venus context.
4. The guest's next wait (`vkWaitRingSeqnoMESA`) failed. Venus aborts on a
   fatal ring error (`vn_relax`: "aborting on ring fatal error"), which is
   the `0xC0000409` in `ucrtbase.dll`.

Type 8 is `VK_OBJECT_TYPE_DEVICE_MEMORY`. This is the "allocation refused"
failure in [`nvva.md`](nvva.md): the host refuses allocations from the
246 MiB `DEVICE_LOCAL | HOST_VISIBLE` heap, the RTX 5090's BAR1, when the
card's BAR1 is nearly full, and it counts card-wide. GZDoom's Vulkan backend
allocates through VMA, which can place buffers the CPU writes in a
device-local, host-visible type. Which GZDoom allocation was refused is
not known: the render server does not log the failed `vkAllocateMemory`. The same demo
passes on `AMD1`, where the heap is not small. `0070` keeps guests off that
heap, and `0086` keeps Zink's mapped buffers working without it.

## What remains

- `host-nvrm.txt` has eight `can't alloc VA space for mapping` lines during
  the `Nvidia1` GZDoom runs. The kernel does not say which process caused
  them, and the card is not named either. Other workers' VMs were running on the NVIDIA cards at the time. Both GZDoom runs were
  clean, and this lab's render-server log has no error.
- GZDoom was not rerun on `Nvidia2`. `Nvidia1` and `Nvidia2` are the same
  card model with the same driver.
