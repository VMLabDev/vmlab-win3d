# DirectDraw: fullscreen exclusive mode and hardware colour-key and mirror blits

`patches/mesa/0012` to `0014` gave DirectDraw a hardware HAL, but two gaps
stayed ([`dxdiag.md`](dxdiag.md#not-covered)). Fullscreen exclusive
DirectDraw had never been run. Colour-key and mirror blits also fell back
to the CPU. This page covers both: the cause, the fix, the evidence and
what is still missing. The work ran on 2026-10-18 on helios: NVIDIA
610.57.04, RADV (Mesa 26.2.2), vmlab 0.28.0-alpha, QEMU 11.1.1 with the
egl-headless patch, and virglrenderer 1.3.0. The evidence is in
`docs/evidence/ddraw/`.

## Result

| Item | Before | After |
| --- | --- | --- |
| Colour-key blits (`DDBLT_KEYSRC`) | The HAL reports no `DDCKEYCAPS_SRCBLT`, so DirectDraw emulates them on the CPU through `Lock`. 50 blits of 128×128 take 440 to 460 ms on Nvidia1 | Drawn on the GPU by `patches/mesa/0035`. The HAL reports `DDCAPS_COLORKEY` and `DDCKEYCAPS_SRCBLT`. 50 blits take 1.4 to 9 ms, including the final readback |
| Mirror blits (`DDBLTFX_MIRRORLEFTRIGHT`, `MIRRORUPDOWN`) | Not in the HAL's `dwFXCaps`, so emulated | Drawn on the GPU. The HAL reports `DDFXCAPS_BLTMIRRORLEFTRIGHT` and `BLTMIRRORUPDOWN` |
| Pixel match with the emulation layer | — | Exact: 13 cases × 3 formats, x64 and x86, on Nvidia1 and AMD1 (`ddblit`) |
| `ddcaps` (0012 to 0014) | Pass | Pass, with the colour-key and mirror steps now `"hal": true` |
| Fullscreen exclusive, 32 bpp (640×480, 800×600, 1024×768), x64 and x86 | Works: the mode set, the flip chain, `Flip`, pixel checks on the front buffer, and restoring the desktop mode | The same |
| Fullscreen exclusive, 16 bpp (640×480×16, 800×600×16), **32-bit (x86) program** | Works. Windows' 32-bit `ddraw.dll` offers 8 and 16 bpp modes and emulates them | The same: 300 flips per mode, every pixel check matching |
| Fullscreen exclusive, 16 bpp, **64-bit (x64) program** | `SetDisplayMode` fails with `DDERR_UNSUPPORTED`: no 16-bit mode is listed | Unchanged ([16 bpp modes](#16-bpp-modes)) |
| Fullscreen frame rate | 2 to 8 flips per second on Nvidia1 | The same; 9 to 29 on AMD1. The Blt-model present limits it, and that present is shared with D3D9 ([Frame rate](#frame-rate)) |

## The repros

The sources are in `docs/evidence/ddraw/src/`, and
`docs/evidence/ddraw/src/build.sh` builds them for x64 and x86 into
`out/ddraw/`. The same script also rebuilds `ddcaps`, which is unchanged
from `docs/evidence/dxdiag/ddcaps/`.

`ddfull.exe [--modes 800x600x32,640x480x16] [--frames N] [--check-every K]`
first lists the modes that DirectDraw (`EnumDisplayModes`) and
`EnumDisplaySettings` offer. It then takes `DDSCL_EXCLUSIVE |
DDSCL_FULLSCREEN` and, for each mode, does the following:

1. Calls `SetDisplayMode` and checks the mode that Windows and DirectDraw
   report.
2. Creates a primary surface with one back buffer, a flip chain.
3. Draws N frames (default 300). Each frame is a colour fill in a new
   colour, three moving bars, a 64×64 sprite blitted with a source colour
   key, and a 16×16 square written through `Lock`.
4. Calls `Flip` for each frame. Every K frames (default every frame), it
   locks the front buffer read-only and checks four pixels: the background,
   the sprite's keyed-out border, the sprite's centre and the CPU square.

At the end it restores the display mode and checks that the desktop mode is
the one it started with.

`ddblit.exe [--timing N] [--map]` runs each blit twice: once on
video-memory surfaces (the HAL) and once on system-memory surfaces. Each
run starts from the same source and destination contents. DirectDraw blits
system-memory surfaces on the CPU, because the HAL does not report
`DDCAPS_CANBLTSYSMEM`, so that run is the emulation layer's (HEL) result.
The program then reads back both 128×128 destinations and compares them
pixel for pixel. It covers three formats (X8R8G8B8, R5G6B5 and A8R8G8B8)
and 13 cases:

- a plain copy;
- a colour key at 1:1, at full size, stretched 2×, shrunk 2×, and at odd
  ratios (37×29 to 91×53);
- mirror left-right, up-down and both, and a mirror with a 2× stretch;
- a mirror combined with a colour key;
- a colour-key blit within one surface.

The A8R8G8B8 source has pixels that carry the key's colour with another
alpha. `DDCREATE_EMULATIONONLY` returns `E_FAIL` on Windows 11, so it
cannot supply the HEL reference. `--map` prints which source row and column
each destination pixel took, for the odd ratio, on both paths.

`docs/evidence/ddraw/run-guest.sh` runs one repro in the autologon desktop
of a lab kept by `tests/e2e/run.sh --keep`. It takes a console screenshot
every 2 s. `WIN3D_TRACE=1` turns on the D3D9 UMD trace for the run.

## Colour-key and mirror blits: `patches/mesa/0035`

### Cause

On WDDM, DirectDraw builds its HAL from the D3D9 UMD's
`GetCaps(D3DDDICAPS_DDRAW)` answer
([`dxdiag.md`](dxdiag.md#how-directdraw-finds-the-driver)). That answer had
`CKeyCaps = 0` and `FxCaps = 0`, and `D3D9Blt` rejected `SrcColorKey`,
`MirrorLeftRight` and `MirrorUpDown` with `E_NOTIMPL`. DirectDraw therefore
emulated every colour-key and mirror blit on video-memory surfaces. It
locked the destination, blended on the CPU, and unlocked it. The 0013 fix
made that correct, but each such blit read the whole destination back from
the GPU and uploaded it again. In the fullscreen repro, the sprite blit read
back the 800×600 back buffer and wrote it back on every frame
(`before/nvidia1/trace-ddfull-800x600-one-frame.txt`).

### Fix

`D3D9Blt` sends source colour-key and mirror blits to `D3D9KeyBlitPipe`.
That function draws one quad into the destination, sampling the source with
a small TGSI fragment shader. The shaders and state objects are made once
per device.

- **Texel mapping.** Before the fix, every case except the odd ratios
  already matched the emulation layer. The odd ratios needed DirectDraw's
  own rule, read from `ddblit --map`. Here s is the source size divided by
  the destination size, and k is the destination pixel's index:
  - With no mirror, pixel k takes source texel `floor((k + 0.5) × s)`, the
    centre of a nearest-neighbour stretch.
  - With any mirror flag, both axes take `floor(k × s)`. On a mirrored
    axis, k counts from the far edge.

  The shader computes this from `gl_FragCoord` and samples at the texel
  centre, so the result is exact. A quarter-step bias keeps float rounding
  away from integer boundaries.
- **Key.** `D3DDDIARG_BLT::ColorKey` is a pixel value in the source format.
  The UMD unpacks it with the format's own conversion. The shader discards
  a texel when every colour channel is within half a step of the key. Alpha
  never takes part, because the emulation layer ignores it too:
  `ddblit`'s A8R8G8B8 pixels with the key's colour and another alpha are
  keyed out by the HEL.
- **Same surface.** A blit within one surface is sampled from a copy of the
  source rectangle, because a surface cannot be sampled while it is the
  render target.
- **Vertices.** The quad's four vertices go through the stream uploader.
  The first version passed them as a user vertex buffer on the stack.
  Yttrium reads vertex data after the draw call returns, so 32-bit
  processes drew garbage. The D3D9 draw path already uploads user vertex
  buffers for the same reason.
- **Caps.** `GetCaps(DDRAW)` reports `DDRAW_CAPS_COLORKEY`,
  `DDRAW_CKEYCAPS_SRCBLT`, `DDRAW_FXCAPS_BLTMIRRORLEFTRIGHT` and
  `DDRAW_FXCAPS_BLTMIRRORUPDOWN`. Destination colour keys, rotation and
  `LinearToSrgb` are still rejected, so DirectDraw still emulates them.

## Fullscreen exclusive mode

### What works

At 32 bpp, fullscreen exclusive DirectDraw works on both GPUs, before and
after 0035:

- `SetDisplayMode` changes the display mode. `EnumDisplaySettings` reports
  the new mode, and the console screenshot changes size.
- `Flip` succeeds for every frame, and every front-buffer pixel check
  matches the frame that was just drawn.
- `RestoreDisplayMode` brings the desktop back to 1280×800.

The KMD offers 19 resolutions at 32 bpp, from 640×480 to 5120×2160
(`ddraw_modes` in `ddfull-*.json`). No KMD change was needed for 32 bpp.

On WDDM with a desktop compositor, DirectDraw does not flip the scanout. A
`Flip` reaches the UMD as a Blt-model `pfnPresent` (`Flags.Blt`, no
destination allocation) of the new front buffer. The KMD copies that on the
CPU, the same path that windowed DirectDraw and D3D9 use. After a `Lock` of
the primary, DirectDraw presents again, so with `--check-every 1` each
frame costs two presents.

### Frame rate

On Nvidia1, fullscreen DirectDraw flips 2 to 8 times per second, both
before and after 0035:

| Run (x64, 120 frames) | 640×480×32 | 800×600×32 | 1024×768×32 |
| --- | --- | --- | --- |
| Before, front buffer checked every 30 frames | 3.6 | 4.4 | 2.2 |
| After, the same | 4.8 | 3.2 | 2.1 |
| After, front buffer checked once | 7.5 | 3.6 | — |
| AMD1 after, front buffer checked every 30 frames | 29.2 | 19.8 | 12.4 |

The rate varies from run to run. In the trace, almost all of each frame is
spent in `pfnPresent`. A Blt-model present takes 65 to 265 ms here
(`before/nvidia1/trace-ddfull-800x600-one-frame.txt`). D3D9 uses the same
present path and reaches 20 fps windowed on Nvidia1
(`after/nvidia1/e2e-results.txt`). That path is covered under "Presentation
pacing" in [`known-limits.md`](known-limits.md), not by this work. 0035
removes the CPU round trip of each colour-key blit, but at these present
costs that does not show in the frame rate.

The present worker found the cause of that cost. In the KMD's Blt present,
the CPU reads write-combined memory: the blob source, and a non-cached
guest-RAM destination. Its fix is two patches: KMD patch 0002, which
caches guest-RAM allocations, and Mesa patch 0026, which puts D3D9 Blt
present sources in host-cached memory. The fullscreen DirectDraw rate is
expected to improve with them. The rates on this page were measured
without them. This work does not change the present path.

### 16 bpp modes

The KMD offers 32-bit display modes only. `AddSingleSourceMode` in
`viogpu/viogpu3d/viogpu_vidpn.cpp` (`arehnman/yttrium-virtio-gpu`) adds each
resolution with `D3DDDIFMT_A8R8G8B8` and `D3DDDIFMT_A8B8G8R8` only. Windows'
display settings therefore show 32-bit modes only for the desktop. What a
DirectDraw program sees depends on its bitness:

- **A 32-bit program** gets 8, 16 and 32 bpp for all 19 resolutions, from
  both `EnumDisplayModes` and `EnumDisplaySettings`. Windows' WOW64
  `ddraw.dll` (SysWOW64) emulates the low-colour modes. `SetDisplayMode(640,
  480, 16)` sets an R5G6B5 primary (masks `f800`, `07e0`, `001f`), and
  `EnumDisplaySettings` in that process reports `640x480x16`. `ddfull` x86
  flipped 300 frames each at 640×480×16 and 800×600×16, and all 1,200
  pixel checks per mode matched. The console shows the frames
  (`after/nvidia1/console-ddfull-x86-*x16.png`). The sprite's colour key
  ran on the GPU on R5G6B5 surfaces (0035).
- **A 64-bit program** gets 32-bit modes only, and `SetDisplayMode` with 16
  bpp fails with `DDERR_UNSUPPORTED` (`0x80004001`). The `16BITCOLOR`
  compatibility layer does not change that
  (`before/nvidia1/ddfull-x64-16bitcolor-layer.json`). Red Hat's
  display-only driver, `viogpudo`, behaves the same in a lab with no GPU
  and no package (`basic/ddfull-x64.json`). That lab flips 800×600×32 at 40
  frames per second.

Games that ask for 16 bpp are 32-bit programs, so they get their mode. On
AMD1, x86 `ddfull` ran 640×480×16 at 19.1 flips per second and 800×600×16
at 13.5, with every pixel check matching
(`after/amd1/ddfull-x86-16bpp.json`).

**The KMD cannot add 16-bit modes for 64-bit programs.** An experiment
showed this:
`kmd-16bpp-experiment/0003-kmd-16bpp-source-modes.patch` adds
`D3DDDIFMT_R5G6B5` to every source mode the VidPN offers, accepts it in the
source-mode check, and maps R5G6B5 standard allocations to
`VIRGL_FORMAT_B5G6R5_UNORM`. With that KMD installed on Nvidia1
(`installed-kmd.txt`), `tests/e2e/run.sh` still passed 12 of 12. A 64-bit
process still saw no 16-bit mode, and `SetDisplayMode(640, 480, 16)` still
failed with `DDERR_UNSUPPORTED` (`kmd-16bpp-experiment/ddfull-x64.json`).
A 32-bit process still got its emulated 16-bit modes
(`kmd-16bpp-experiment/ddfull-x86.json`).

Windows does not pass sub-32-bit source modes from this driver on to
programs. Low colour depths reach programs only through the 32-bit
DirectDraw emulation. The patch therefore stays out of `patches/kmd`. The
desktop keeps 32-bit modes only, as before. A 64-bit DirectDraw program
that insists on 16 bpp has no path on this stack, and we know of no such
program.

## Proof

The package was built at `cbbc481` with `patches/mesa` 0001 to 0025 and
0035 (`after/<gpu>/e2e-build.txt`). Each GPU got a fresh
`x86_64/windows-11@26100.1742.5` lab through `tests/e2e/run.sh --host-gpu
<gpu> --keep`. The repros then ran in the same lab, with the installed UMD
(`run-guest.sh`).

| Check | Nvidia1 (RTX 5090) | AMD1 (RADV) |
| --- | --- | --- |
| `tests/e2e/run.sh --template windows-11` | 12 of 12 | 12 of 12 |
| `ddblit`, x64 and x86 | Pass: 13 cases × 3 formats match the HEL exactly. 50 colour-key blits take 2.0 to 10.4 ms, against 438 to 464 ms before 0035 | Pass, the same. 50 colour-key blits take 1.4 to 3.2 ms |
| `ddcaps`, x64 and x86 | Pass. Colour key and mirror are HAL steps; 87 and 89 windowed blits in 10 s | Pass. 147 and 143 windowed blits in 10 s |
| `ddfull` x64, 800×600×32, 300 frames, every frame checked | Pass, 1,200 of 1,200 pixel checks, 2.3 flips per second; 640×480×16 `DDERR_UNSUPPORTED` | Pass, 1,200 of 1,200, 9.4 flips per second; 640×480×16 `DDERR_UNSUPPORTED` |
| `ddfull` x86, 800×600×32 and 640×480×16, then 640×480×16 and 800×600×16, 300 frames each | Pass, every pixel check, desktop mode restored | Pass, every pixel check: 800×600×32 at 9.3 flips per second; 640×480×16 at 17.8 and 19.1; 800×600×16 at 13.5 |
| Host Xid during the runs | none | not applicable |

## Upstream

0035 belongs in `arehnman/virtio-win-mesa`, with 0012 to 0014. It changes
only the D3D9 UMD's DirectDraw paths: `D3D9Blt` with the colour-key or
mirror flags, which only DirectDraw sends, and `GetCaps(DDRAW)`.

## Evidence index (`docs/evidence/ddraw/`)

| Path | What it shows |
| --- | --- |
| `src/ddfull.c`, `src/ddblit.c`, `src/build.sh` | The repros and their build in the project toolchain image |
| `run-guest.sh` | Runs a repro in a kept e2e lab's desktop, with console captures and an optional UMD trace |
| `before/nvidia1/ddfull-x64.json` | Package `b4b8c3f`: the mode lists (32 bpp only), 640×480, 800×600 and 1024×768 at 3.6, 4.4 and 2.2 flips per second with every pixel check matching, and 640×480×16 failing with `0x80004001` |
| `before/nvidia1/ddfull-x64-16bitcolor-layer.json` | The same with the `16BITCOLOR` compatibility layer: still no 16-bit mode |
| `before/nvidia1/console-ddfull-*.png` | The console in each fullscreen mode |
| `before/nvidia1/trace-ddfull-800x600-one-frame.txt` | One frame of the 800×600 run: the emulated colour key's back-buffer readback and the 265 ms Blt-model present |
| `before/nvidia1/ddblit-x64.json`, `ddcaps-x64.json` | No colour-key or mirror caps; the emulated blits match the HEL and take 440 to 460 ms for 50 |
| `basic/ddfull-x64.json`, `basic/video-controller.txt` | Red Hat's display-only `viogpudo` (no package, no GPU): the same 32-bit-only mode list and the same 16-bit failure |
| `after/<gpu>/ddblit-{x64,x86}.json` | 0035: every case matches the HEL exactly, with the timing |
| `after/<gpu>/ddcaps-{x64,x86}.json` | 0012 to 0014 still pass; colour key and mirror are HAL steps |
| `after/<gpu>/ddfull-x64.json`, `console-ddfull-x64-800x600x32.png` | x64 fullscreen 800×600×32, 300 frames, every frame checked; 640×480×16 refused |
| `after/<gpu>/ddfull-x86-16bpp.json`, `ddfull-x86-32and16.json`, `console-ddfull-x86-*x16.png` | x86 640×480×16 and 800×600×16, 300 frames each, all checks matching; screenshots during the 16 bpp flip loop |
| `after/<gpu>/ddfull-x64-rate.json`, `after/nvidia1/ddfull-x64-nocheck.json` | Flip rates at 640×480, 800×600 and 1024×768 |
| `after/<gpu>/e2e-results.txt`, `e2e-build.txt`, `e2e-host.txt`, `qemu-argv-env.txt` | `tests/e2e/run.sh` on the package with 0035: 12 of 12 |
| `kmd-16bpp-experiment/` | The R5G6B5 source-mode KMD patch (not applied), its installed hash, its e2e result (12 of 12), and `ddfull` x64 and x86 with it: no change |
