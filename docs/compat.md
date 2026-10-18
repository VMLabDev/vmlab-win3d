# Application compatibility sweep

Before this sweep, the only third-party programs the package had run were
the DirectX SDK samples and Blender (`docs/acceptance.md`, "Stretch"). This
sweep runs six unmodified, freely downloadable Windows programs, 12 runs that
cover Direct3D 9, 11 and 12, OpenGL and Vulkan, on the installed package. It
ran on AMD1 (the AMD iGPU, RADV) and on Nvidia2 (the second RTX 5090, with
the patched QEMU). It reports what happened and does not fix anything. Each
failure below is recorded in enough detail for a later worker to pick it up.

- AMD1: 10 of 12 runs pass. Heaven D3D11 is counted as a pass because its
  rerun was clean, but its first run crashed.
- Nvidia2: 5 of 12 runs pass. Godot's Vulkan renderer causes a new NVIDIA
  **Xid 69** (ErrorCode `0x13d`) every time it starts.
- Both GPUs: GZDoom cannot create an OpenGL context.

## How to rerun it

```sh
tests/apps/compat/run.sh --host-gpu AMD1      # or Nvidia2; --help for the options
```

The script (`tests/apps/compat/run.sh`, with the runs in
`tests/apps/compat/apps.sh`) does the following:

1. It brings up a fresh `windows-11` lab, `win3d-compat-<gpu>`, from
   `labs/acceptance/vmlab.wcl` with a NAT NIC.
2. It activates the evaluation edition with `slmgr /ato`. Without this step,
   both of the first labs shut down after an hour.
3. It sets up the autologon desktop and installs the bundle with
   `scripts/install-into-vm.sh`.
4. It disables Windows Update in the guest so that the update service does
   not compete for the host's slow shared link.
5. It downloads the applications pinned in `tests/apps/compat/PINS` and
   checks each sha256. It then installs each application the way a user
   would: the Unigine installers run silently, and the zips are unpacked
   under `C:\apps`.
6. It runs each program for 150 s plus about 20 s of start-up in the
   autologon user's session, through `tests/guest/Run-Probe.ps1`. During the
   run it takes a console screenshot every 5 s.
7. It closes the program with WM_CLOSE to the program's window
   (`tests/apps/compat/Close-App.ps1`), as the window's close button would.
   A program must exit by itself within 60 s, or it fails.

A run is **clean** when the program exits with code 0 after the close and
did not time out.

### What each run records

Each run writes its evidence to `docs/evidence/compat/<gpu>/<run>/`:

- `console-1.png`, `console-2.png` and `console-3.png`: console frames at
  about 20 s, at mid-run and just before the close.
- `console-closing.png`: the console 10 s after the close. Only the
  reruns, `<gpu>/crispy-doom-d3d9` and `amd1/heaven-d3d11`, have it.
- `console-after.png`: the console after the program exited.
- `run.json`: the exit code, the duration and the graphics modules.
- `modules.txt`: every module the program loaded. The graphics modules come
  from System32 or SysWOW64, and no run loaded an app-local graphics runtime.
  Heaven and Valley ship only the D3DX helper DLLs (`d3dx9_42`,
  `d3dx11_42`, `d3dcompiler_42`).
- `yttrium-log.txt`: the UMD's warnings for this process, counted.
- `qemu-log.txt`: the QEMU and render-server lines written during the run.
- `host-kernel.txt`: the host kernel lines for this lab's GPU (NVIDIA Xid
  for `PCI:0000:05:00`, or `amdgpu`).
- `fps.txt`: the frame rate, where the program reports one.
- `godot.log` or `gzdoom.log`: the program's own log.
- For a failed run, `guest-errors.txt` (event logs, the UMD log tail and open
  windows) and `wer.txt` (the program's Windows Error Reporting reports).

`results.json` in each GPU directory summarises the runs.

### Frame rates

Godot reports its own frame rate: `--print-fps`, averaged after the first
10 s. GZDoom (`vid_fps`) and the Unigine benchmarks draw an FPS counter on
the screen. The script reads that counter with tesseract from each frame
between 20 s and the close. It found the counter in 30 to 50 % of the
Unigine frames, so those figures are means of a sample, not true averages.
The free Unigine editions have no CSV or command-line benchmark mode (those
are paid features), so the sweep does not use their F9 benchmark.

### Setup the evidence comes from

| Item | Value |
| --- | --- |
| Package | `vmlab-win3d-0.1.1`, the rc bundle built at `0ee5ba1` (`82d8e51` plus the version bump). The zip's sha256 is `414117aa...`, the same as `docs/evidence/rc/` |
| Host | QEMU 11.1.1 (the patched QEMU for Nvidia2), virglrenderer 1.3.0, Mesa 26.2.2, NVIDIA 610.57.04, vmlab 0.28.0-alpha (`<gpu>/host.txt`) |
| Guest | `windows-11@26100.1742.5`, activated (`<gpu>/activation.txt`) |
| Date | 2026-10-19 |

The 24 runs came from one pass on each GPU. Four runs were then repeated
with `--reuse --only`: `amd1/heaven-d3d11` and both `crispy-doom-d3d9`
runs, to add the closing frame and to check whether the Heaven crash
recurs, and `nvidia2/gzdoom-vulkan`, to confirm its crash. The first AMD1
Heaven D3D11 run is kept as `amd1/heaven-d3d11-run1`. After those runs, the
script was changed in three ways, and the earlier evidence was filtered the
same way:

- `host-kernel.txt` keeps only the lab's own GPU. Other workers' NVIDIA VMs
  log `NVRM` lines on the same host.
- `wer.txt` keeps only the reports written during the run.
- The closing frame was added.

## Matrix

"Pass" means the program rendered correctly in the console frames, ran the
full time, and exited cleanly on WM_CLOSE.

| Application | API | GPU | Result | FPS | Evidence |
| --- | --- | --- | --- | --- | --- |
| Unigine Heaven 4.0 | D3D11 | AMD1 | Pass on the rerun; the first run crashed at 65 s (heap corruption, [F7](#f7-heaven-d3d11-on-amd1-crashed-once)) | 15.7 (OCR) | [amd1/heaven-d3d11](evidence/compat/amd1/heaven-d3d11/), [run1](evidence/compat/amd1/heaven-d3d11-run1/) |
| Unigine Heaven 4.0 | D3D11 | Nvidia2 | Pass | 26.6 (OCR) | [nvidia2/heaven-d3d11](evidence/compat/nvidia2/heaven-d3d11/) |
| Unigine Heaven 4.0 | D3D9 | AMD1 | Pass | 13.0 (OCR) | [amd1/heaven-d3d9](evidence/compat/amd1/heaven-d3d9/) |
| Unigine Heaven 4.0 | D3D9 | Nvidia2 | **Renders wrong**: dark frames and skipped draws ([F5](#f5-d3d9-depth-bias-on-nvidia)); exits cleanly | 1.1 (OCR) | [nvidia2/heaven-d3d9](evidence/compat/nvidia2/heaven-d3d9/) |
| Unigine Heaven 4.0 | OpenGL | AMD1 | Pass | 17.5 (OCR) | [amd1/heaven-opengl](evidence/compat/amd1/heaven-opengl/) |
| Unigine Heaven 4.0 | OpenGL | Nvidia2 | **Crash** at 26 s, `0xC0000005` in `viogpu_wgl.dll` ([F4](#f4-unigine-opengl-on-nvidia2)) | - | [nvidia2/heaven-opengl](evidence/compat/nvidia2/heaven-opengl/) |
| Unigine Valley 1.0 | D3D11 | AMD1 | Pass | 14.8 (OCR) | [amd1/valley-d3d11](evidence/compat/amd1/valley-d3d11/) |
| Unigine Valley 1.0 | D3D11 | Nvidia2 | Pass | 9.8 (OCR) | [nvidia2/valley-d3d11](evidence/compat/nvidia2/valley-d3d11/) |
| Unigine Valley 1.0 | D3D9 | AMD1 | **Hang**: the frame freezes mid-run, and the program does not exit on WM_CLOSE ([F6](#f6-valley-d3d9-on-amd1-hangs)) | 2.7 (OCR) | [amd1/valley-d3d9](evidence/compat/amd1/valley-d3d9/) |
| Unigine Valley 1.0 | D3D9 | Nvidia2 | **Crash** at 111 s, `0xC0000005` in `Unigine_x86.dll`, after skipped draws ([F5](#f5-d3d9-depth-bias-on-nvidia)) | 3.9 (OCR) | [nvidia2/valley-d3d9](evidence/compat/nvidia2/valley-d3d9/) |
| Unigine Valley 1.0 | OpenGL | AMD1 | Pass | 20.8 (OCR) | [amd1/valley-opengl](evidence/compat/amd1/valley-opengl/) |
| Unigine Valley 1.0 | OpenGL | Nvidia2 | **Crash** at 17 s, `0xC0000409`, with no frame ([F4](#f4-unigine-opengl-on-nvidia2)) | - | [nvidia2/valley-opengl](evidence/compat/nvidia2/valley-opengl/) |
| Godot 4.7.2, 3D Particles demo | D3D12 | AMD1 | Pass | 59.2 | [amd1/godot-d3d12](evidence/compat/amd1/godot-d3d12/) |
| Godot 4.7.2, 3D Particles demo | D3D12 | Nvidia2 | Pass | 59.8 | [nvidia2/godot-d3d12](evidence/compat/nvidia2/godot-d3d12/) |
| Godot 4.7.2, 3D Particles demo | Vulkan | AMD1 | Pass | 60.1 | [amd1/godot-vulkan](evidence/compat/amd1/godot-vulkan/) |
| Godot 4.7.2, 3D Particles demo | Vulkan | Nvidia2 | **Xid 69** on the host about 9 s in, no frame drawn, then a crash with `0xC0000409` ([F1](#f1-godot-vulkan-on-nvidia2-xid-69)) | - | [nvidia2/godot-vulkan](evidence/compat/nvidia2/godot-vulkan/) |
| Godot 4.7.2, 3D Particles demo | OpenGL 3.3 (Compatibility) | AMD1 | Pass ([note](#godot-compatibility-renderer-looks-lighter)) | 62.8 | [amd1/godot-opengl](evidence/compat/amd1/godot-opengl/) |
| Godot 4.7.2, 3D Particles demo | OpenGL 3.3 (Compatibility) | Nvidia2 | Pass ([note](#godot-compatibility-renderer-looks-lighter)) | 59.0 | [nvidia2/godot-opengl](evidence/compat/nvidia2/godot-opengl/) |
| GZDoom 4.14.2, Freedoom 2 | Vulkan | AMD1 | Pass | 35.0 (OCR) | [amd1/gzdoom-vulkan](evidence/compat/amd1/gzdoom-vulkan/) |
| GZDoom 4.14.2, Freedoom 2 | Vulkan | Nvidia2 | **Crash** at 6 to 26 s, `0xC0000409`, with a black window; 2 of 2 runs ([F2](#f2-gzdoom-vulkan-on-nvidia2)) | - | [nvidia2/gzdoom-vulkan](evidence/compat/nvidia2/gzdoom-vulkan/) |
| GZDoom 4.14.2, Freedoom 2 | OpenGL | AMD1 | **Fails to start**: "Unable to create an OpenGL render context" ([F3](#f3-gzdoom-cannot-create-an-opengl-context)) | - | [amd1/gzdoom-opengl](evidence/compat/amd1/gzdoom-opengl/) |
| GZDoom 4.14.2, Freedoom 2 | OpenGL | Nvidia2 | **Fails to start**: the same as AMD1 | - | [nvidia2/gzdoom-opengl](evidence/compat/nvidia2/gzdoom-opengl/) |
| Crispy Doom 7.2.0, Freedoom 2 | D3D9 (SDL renderer) | AMD1 | Pass; on WM_CLOSE it asks "press y to quit", by design ([note](#crispy-doom-asks-before-it-quits)) | - | [amd1/crispy-doom-d3d9](evidence/compat/amd1/crispy-doom-d3d9/) |
| Crispy Doom 7.2.0, Freedoom 2 | D3D9 (SDL renderer) | Nvidia2 | Pass; the same quit prompt | - | [nvidia2/crispy-doom-d3d9](evidence/compat/nvidia2/crispy-doom-d3d9/) |

By API:

| API | AMD1 | Nvidia2 |
| --- | --- | --- |
| D3D11 | 2 of 2 pass; Heaven crashed in 1 of 2 runs | 2 of 2 pass |
| D3D9 | 2 of 3 pass; Valley hangs | 1 of 3 pass; both Unigine runs fail (F5) |
| OpenGL | 3 of 4 pass; GZDoom fails (F3) | 1 of 4 pass; only Godot |
| Vulkan | 2 of 2 pass | 0 of 2 pass; Xid 69 (F1) and a crash (F2) |
| D3D12 | 1 of 1 passes | 1 of 1 passes |

## Failures

The Yttrium UMD log is `C:\ProgramData\Yttrium\yttrium-errors.log`. The
render-server lines are virglrenderer's, from QEMU's log.

### F1: Godot Vulkan on Nvidia2: Xid 69

- **Repro:**
  `Godot_v4.7.2-stable_win64.exe --path C:\apps\godot\particles --rendering-driver vulkan`.
  This is the Forward+ renderer with the official 3D Particles demo.
- **What happens:** Godot creates its Vulkan device: "Vulkan 1.4.334 -
  Forward+ - Using Device #0: NVIDIA - Virtio-GPU Venus". About 9 s after
  start, the host logs an Xid 69 from that lab's render server. The window
  stays grey, and Godot prints no FPS line. Godot then fails fast with
  `0xC0000409` in `ucrtbase.dll` (an abort): at 19 s in the sweep, and at
  60 s in the first trial.
- **The two Xids, matched to the run logs** (host times, AEST; the run log
  is `.scratch/compat/nvidia2/run.log`, not committed). Both came from lab
  `win3d-compat-nvidia2`, and no other run of this lab was active at either
  moment:

  | NVRM line (verbatim) | Run | Run started | Seconds in |
  | --- | --- | --- | --- |
  | `NVRM: Xid (PCI:0000:05:00): 69, pid=2238723, name=vkr-ring-37, Class Error: channel 0x00000038, Class 0000ce97, Offset 0000026c, Data 00000006, ErrorCode 0000013d` at 01:50:44 | `godot-vulkan`, first trial run (`--seconds 30`, in a lab later recreated after the evaluation shutdown; [nvidia2-trial-godot-vulkan](evidence/compat/nvidia2-trial-godot-vulkan/)) | 01:50:35 | 9 |
  | `NVRM: Xid (PCI:0000:05:00): 69, pid=3048572, name=vkr-ring-70, Class Error: channel 0x00000052, Class 0000ce97, Offset 0000026c, Data 00000006, ErrorCode 0000013d` at 03:04:13 | `godot-vulkan`, the sweep (`nvidia2/godot-vulkan/host-kernel.txt`) | 03:04:04 | 9 |

  In both runs, the pid is the run's `virgl_render_server` process, the one
  that logs the CS errors below. ffp2 traced the same two lines to this lab
  (`docs/evidence/ffp2/host-nvrm.txt` on main).
- **Render server:** `vkQueueSubmit resulted in CS error`, then
  `vkCreateGraphicsPipelines resulted in CS error` twice.
- **Reproducibility:** 2 of 2 runs, both about 9 s after start.
- **Other GPUs and renderers:** the same demo passes on AMD1 at 60 fps, and
  on Nvidia2 under D3D12 (vkd3d-proton over the same Venus device) and
  OpenGL.
- **Diagnosis:** `patches/mesa/0022` fixed Xid 69 for D3D9 depth draws
  (ErrorCode `0x9c`, `docs/xid69.md`). This is a different error code, from a
  native Vulkan application, so the UMD is not involved.

**Status:** fixed by `patches/mesa/0075` ([godot.md](godot.md)). The Win32
WSI gave Vulkan applications LINEAR swapchain images, and NVIDIA faults on a
draw into a LINEAR colour target when the pipeline has fragment-shading-rate
state, which Godot sets on every pipeline. Swapchains now render into an
OPTIMAL image where the device lacks linear colour-attachment support. Godot
Vulkan then ran clean on Nvidia1 at 58.4 fps with no Xid, and its D3D12 and
OpenGL runs stayed clean.

### F2: GZDoom Vulkan on Nvidia2

- **Repro:** `gzdoom.exe -iwad freedoom2.wad -config compat-1.ini`, with
  `vid_preferbackend=1`.
- **What happens:** GZDoom finds "Vulkan device: Virtio-GPU Venus (NVIDIA
  GeForce RTX 5090)". The window stays black. GZDoom fails fast with
  `0xC0000409` (`ucrtbase.dll`+`0xa527e`) after 6 s in the rerun and 26 s in
  the first run.
- **Render server:** `failed to look up object 76 of type 8`,
  `vkFreeMemory resulted in CS error`,
  `vkWaitRingSeqnoMESA resulted in CS error`,
  `failed to dispatch context op 5`, then the context is destroyed "with a
  valid instance".
- **Host kernel:** no Xid.
- **Other GPUs:** AMD1 passes at 35 fps.

### F3: GZDoom cannot create an OpenGL context

This happens on both GPUs.

- **Repro:** `vid_preferbackend=0`.
- **What happens:** GZDoom loads `viogpu_wgl.dll`, then stops with a fatal
  error dialog:

  ```
  R_OPENGL: Unable to create an OpenGL render context. Insufficient driver support for context creation
  ```

  The log is in `gzdoom.log`. The dialog stays until WM_CLOSE, and the
  program then exits with -1.
- **Other OpenGL programs:** Heaven and Valley on AMD1, Godot's GL 3.3
  Compatibility renderer and Blender 4.2 (GL 4.3, acceptance) all create
  contexts.
- **Next step:** trace GZDoom's WGL calls (pixel format attributes and
  `wglCreateContextAttribsARB` attributes) against what `viogpu_wgl.dll`
  accepts. The cause is not diagnosed here.

### F4: Unigine OpenGL on Nvidia2

Both Unigine OpenGL runs crash on Nvidia2. Both pass on AMD1.

Heaven:

- It renders for a while, then crashes at 26 s with `0xC0000005` in
  `viogpu_wgl.dll`+`0x2699c8` (`wer.txt`).
- Render server: `vkBindBufferMemory2 resulted in CS error`, then
  `failed to look up object 17379 of type 8`.

Valley:

- It draws no frame and fails fast at 17 s with `0xC0000409`
  (`ucrtbase.dll`+`0x2da71`).
- Render server: `vkCreateGraphicsPipelines resulted in CS error`,
  `vkBindBufferMemory2 resulted in CS error` and `failed to look up object
  11110 of type 8`.

F1, F2 and F4 share the render-server pattern: a Venus command fails on the
NVIDIA host, and then a later command names an object the server no longer
has. They might have one cause, but that is not established.

### F5: D3D9 depth bias on NVIDIA

On Nvidia2, the UMD log of both Unigine D3D9 runs (`yttrium-log.txt`) is
dominated by these lines:

```
69686 yttrium: unscaled depth bias requires Vulkan float representation
69686 yttrium: shader_draw_probe try_draw skipped no pipeline res_id=N
53727 yttrium: shader_draw_probe pipeline create failed ... topology=3 ...
 9958 d3d9ddi: draw failed owner=d3d9umd reason=backend_native_draw_failed action=return_E_FAIL
```

The counts are Heaven's. Valley has 18499 of the first two lines and 893
failed draws.

D3D9 depth bias is unscaled, so the UMD asks Vulkan for a float depth-bias
representation (`VK_EXT_depth_bias_control`). The NVIDIA Venus device does
not give it, so every pipeline with depth bias fails and its draws are
skipped. The AMD1 runs log none of these lines.

- **Heaven:** about a third of the frames are dark, and the scene runs at
  about 1 fps. It still exits cleanly.
- **Valley:** it renders partly, then crashes at 111 s with `0xC0000005` in
  `Unigine_x86.dll`+`0x3cc176`. A draw that returns `E_FAIL` is a plausible
  trigger, but that is not proven.

The fix belongs in the UMD: a fallback that scales the bias when the float
representation is missing.

**Status:** fixed by `patches/mesa/0090` ([unid3d.md](unid3d.md#f5-d3d9-depth-bias-on-nvidia)).
Heaven D3D9 now renders correctly on NVIDIA. Valley D3D9 and Heaven's exit
still fail there, from F6's cause.

### F6: Valley D3D9 on AMD1 hangs

- **What happens:** the frame freezes partway through the run. The middle
  and last frames are identical, and the sampled counter shows 2.7 fps.
- **Render server:** `vkCmdBeginRenderPass resulted in CS error`.
- **On close:** after WM_CLOSE, the program does not exit within 60 s, and
  `Run-Probe` kills it.
- **UMD log:** no lines for the process.

**Status:** cause found, not fixed ([unid3d.md](unid3d.md#f6-valley-d3d9-on-amd1)).
Valley, a 32-bit process, runs out of address space through the UMD's
ordered-context-worker upload path. `ORDERED_CONTEXT_WORKER=0` in
`C:\ProgramData\Yttrium\yttrium.ini` works around it.

### F7: Heaven D3D11 on AMD1 crashed once

- **First run (`heaven-d3d11-run1`):** Heaven rendered, then crashed at
  65 s with `0xC0000374` (heap corruption, detected in `ntdll`; WER bucket
  `StackHash_196c`). There were no render-server or UMD lines.
- **Rerun:** clean, at 15.7 fps.
- **Nvidia2:** clean, at 26.6 fps.

The crash is intermittent. A heap check (Application Verifier or page heap)
on `Heaven.exe` is the next step.

**Status:** reproduced under PageHeap in 2 of 5 runs, as a use-after-free in
the UMD's threaded context. Not fixed ([unid3d.md](unid3d.md#f7-heaven-d3d11-on-amd1)).

## Notes that are not failures

### Godot Compatibility renderer looks lighter

Under OpenGL, the 3D Particles demo looks lighter and more washed out than
under Forward+, on both GPUs. The Compatibility renderer has simpler
lighting and tonemapping, so some difference is expected. The output has not
been compared against a reference renderer, so a colour-space problem in the
GL path is not ruled out.

### Crispy Doom asks before it quits

Chocolate Doom and Crispy Doom answer a window close in game with the quit
confirmation ("press y to quit"; `<gpu>/crispy-doom-d3d9/console-closing.png`).
No key is sent, so `Run-Probe` kills the program after 60 s. The program
renders correctly through `D3D9.DLL`, which is SDL's default renderer here
(`modules.txt`).

### Black first frames

Some Unigine runs (Heaven D3D9 on AMD1, and Valley D3D9 and D3D11 on
Nvidia2) show a black window in the frame at about 20 s, while the benchmark
is still loading. The later frames render, so this is start-up, not a fault.

## What was left out, and why

- **Unigine Superposition:** its EULA
  (`https://benchmark.unigine.com/policies/superposition-eula`) limits the
  free Basic edition to "private individuals ... solely for their own use".
  Organisations need the Pro edition. The download is also 1.3 GB, on a host
  link that ran at about 0.5 MB/s that day.

  The Heaven and Valley EULAs allow any number of copies "for educational
  and evaluation purposes" and do not mention automation. The sweep uses
  only what the free editions offer: the launcher's command line and the
  on-screen counter.
- **Sascha Willems' Vulkan examples:** the precompiled Windows binaries are
  gone. `vulkan.gpuinfo.org/downloads/examples/` returns 404. GZDoom and
  Godot cover Vulkan instead.
- **A D3D12 benchmark:** no free D3D12 benchmark could be downloaded without
  an account or store. 3DMark's free edition is Steam-only. Godot's D3D12
  renderer is the D3D12 application here.
- **Godot exports:** the sweep runs the official demo project with the
  official engine binary (`--path`), not an exported game. An export
  embeds the same engine minus the editor. Exporting needs the export
  templates, a further 1 GB download.

## What remains

- F1: fixed (`patches/mesa/0075`, [godot.md](godot.md)).
- F2 and F4 on NVIDIA: render-server CS errors from native Vulkan and from
  Zink.
- F3: GZDoom's OpenGL context.
- F5: fixed (`patches/mesa/0090`).
- F6: Valley D3D9 runs out of 32-bit address space ([unid3d.md](unid3d.md)).
- F7: the threaded-context use-after-free behind Heaven D3D11's heap corruption ([unid3d.md](unid3d.md)).
- Not run: Windows 10, Nvidia1, x86 builds other than Unigine (which is
  32-bit only), and longer runs.
