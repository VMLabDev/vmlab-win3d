# dxdiag: DirectDraw and AGP acceleration

With the package installed, dxdiag on Windows 11 used to report the display
device as follows:

| Field | Before | After |
| --- | --- | --- |
| DDraw Status | Not Available | Enabled |
| D3D Status | Enabled | Enabled |
| AGP Status | Not Available | Enabled |
| `Get-Dxdiag.ps1` `hardware_acceleration` | `false` | `true` |

Three Mesa patches against the D3D9 user-mode driver (UMD) fix it:
`patches/mesa/0012` to `0014`. No KMD change was needed. The evidence is in
`docs/evidence/dxdiag/`.

## How DirectDraw finds the driver

On WDDM there is no DirectDraw kernel driver. `ddraw.dll` loads the D3D9 UMD
named in the display key's `UserModeDriverName` (`viogpu_d3d10.dll`, which
exports `OpenAdapter`), opens it with runtime interface 7 and builds its HAL
from `pfnGetCaps` answers. The D3D9 runtime uses interface 9. dxdiag does the
same thing in its own process. The UMD trace shows the sequence for both
`ddcaps.exe` and `dxdiag.exe` (`docs/evidence/dxdiag/before/getcaps-trace.log`):

| Order | `D3DDDICAPS_*` | Type | Answer before 0012 |
| --- | --- | --- | --- |
| 1 | `DDRAW` | 1 | `DDRAW_CAPS`: Caps `ZBLTS \| BLTDEPTHFILL`, Caps2 `DYNAMICTEXTURES` |
| 2 | `DDRAW_MODE_SPECIFIC` | 2 | zeroed (no overlays) |
| 3 | `GETGAMMARAMPCAPS` | 34 | `GAMMA_CAP_RGB256x3x16` |
| 4 | `GETFORMATCOUNT`, `GETFORMATDATA` | 3, 4 | 46 formats |
| 5 | `GETD3D3CAPS` | 8 | `D3DHAL_GLOBALDRIVERDATA` |
| 6 | `GETD3D7CAPS` | 11 | **`E_NOTIMPL`** |

After the failed `GETD3D7CAPS` call, DirectDraw discards the HAL. The program
still gets a DirectDraw object, but it runs on the software emulation layer
(HEL) only:

- `IDirectDraw7::GetCaps` returns HAL `dwCaps = DDCAPS_NOHARDWARE` and no
  blit caps.
- A `DDSCAPS_VIDEOMEMORY` surface fails with `DDERR_NODIRECTDRAWHW`
  (`0x88760233`).
- `IDirect3D7::EnumDevices` lists only "RGB Emulation".

Evidence: `docs/evidence/dxdiag/before/ddcaps-x86.json`.

dxdiag shows that state as "DDraw Status: Not Available". D3D Status was
already Enabled, because the D3D9 runtime never asks for the D3D7 caps.

### AGP Status

AGP Status changed together with DDraw Status. No KMD change was involved.
With a working HAL, DirectDraw reports `DDCAPS2_NONLOCALVIDMEM`, and
`GetAvailableVidMem(DDSCAPS_NONLOCALVIDMEM | DDSCAPS_TEXTURE)` returns
4,258,091,008 bytes. That figure matches the 4092 MB of shared memory that
dxdiag lists, so a virtual GPU without an AGP aperture still has non-local
memory. Without a HAL, DirectDraw reports no non-local memory, and dxdiag
shows AGP as Not Available.

This dependence is inferred from the before and after data. Microsoft does
not document dxdiag's exact rule. The Microsoft Basic Render Driver shows
the same pattern, with DDraw and AGP both Enabled
(`docs/evidence/probes/dxdiag.json`).

## The fixes

### 0012: report the D3D7 and D3D8 caps

`patches/mesa/0012-d3d9-directdraw-hal-caps.patch` adds three things:

- `D3DDDICAPS_GETD3D7CAPS` returns `D3DHAL_D3DEXTENDEDCAPS` (116 bytes,
  mirrored locally like the D3D3 structure, because `d3dhal.h` clashes with
  the D3D9 headers). The values come from the existing `D3DCAPS9`: texture
  limits, guard band, stencil, FVF, texture-op, light and clip-plane caps.
- `D3DDDICAPS_GETD3D8CAPS` returns `D3DCAPS8` (the first 212 bytes of
  `D3DCAPS9`) with shaders capped at vs_1_1 and ps_1_4. Neither dxdiag nor
  ddraw asked for it in these runs. It is there so that a D3D8-era caller
  does not hit the same `E_NOTIMPL`.
- The D3D3 global driver data now has its line and triangle `D3DPRIMCAPS`,
  lighting and transform caps, with the matching `D3DDD_*` valid flags. They
  were all zero. Once the HAL exists, DirectDraw exposes the "Direct3D HAL"
  and "Direct3D T&L HAL" devices to 32-bit Direct3D 7 applications, and those
  devices read their primitive caps from this structure.

This patch alone makes dxdiag report DDraw, D3D and AGP as Enabled. Testing
DirectDraw itself found two more bugs.

### 0013: keep contents across a write-only lock

DirectDraw advertises only what the UMD can do in hardware. It emulates the
rest, such as colour-key and mirror blits, by locking the destination surface
and writing on the CPU. The emulation layer locks the **whole** destination
write-only (`D3DDDI_LOCKFLAGS.WriteOnly`, without `Discard`) and writes only
the blitted rectangle. The UMD mapped the GPU texture write-only and uploaded
the whole box from a staging copy that never held the surface contents, so
everything outside the rectangle became black. In the ddcaps blit test,
every hardware copy and stretch result was lost and only the emulated mirror
blit survived (`docs/evidence/dxdiag/before/ddcaps-x64-0012-only.json`).

`patches/mesa/0013-d3d9-write-only-lock-keeps-contents.patch` sends locks
that are write-only without `Discard` through the existing readback path,
which starts from the current contents. D3D9 applications rarely
lock textures write-only without `Discard`, and the D3D9 probes show no
change.

### 0014: DirectDraw surfaces are display targets

A windowed DirectDraw program blits to the primary surface through a
clipper. On WDDM that blit reaches the UMD as `pfnPresent`
(`Flags.Blt`, no destination). The source is the application's off-screen
plain surface, created with no `RenderTarget` or `Texture` flag. Without
`PIPE_BIND_DISPLAY_TARGET`, that surface was a tiled device-local image that
the KMD present could not read. Every call returned `S_OK`, but the window
stayed empty (`docs/evidence/dxdiag/before/console-ddraw-window-0012-only.png`).
A D3D9 swapchain back buffer gets `PIPE_BIND_DISPLAY_TARGET`; the trace
shows bind `0x100082` for a D3D9 back buffer and `0x100002` for a DirectDraw
surface.

`patches/mesa/0014-d3d9-ddraw-surfaces-are-present-sources.patch` records
the runtime interface at `CreateDevice`. For interfaces below 9 (DirectDraw
and Direct3D 7 and earlier), it creates plain colour surfaces in video memory
as display targets. D3D9 devices are unchanged.

## Evidence

All runs used one throwaway lab, `win3d-fix-dxdiag-1`, built from
`labs/probe/vmlab.wcl` (Windows 11 26100.1742.5 template, AMD iGPU Venus,
vmlab 0.24.0-alpha). The lab was destroyed after the runs. The package was
installed with `scripts/install-into-vm.sh`. Before the fix, the bundle was
the e2e worker's build of `main` at `be2ef9b`. After the fix, it was
`scripts/build.sh --package-only` over `build/mesa/build.sh` with patches
0001 to 0014. Installed UMD hashes after the fix (signed copies from the
bundle):

```
9d5d5ed09461fb44283a6a18ab0498180ef630aaaf2ecdc93a8335e012c4a6b3  driver/x64/viogpu_d3d10_x64.dll
a028267940944a4554cbb10af303e705d342b695bbba9a5f452509d9246f0a5a  driver/x86/viogpu_d3d10_x86.dll
```

| Path | What it shows |
| --- | --- |
| `docs/evidence/dxdiag/before/dxdiag.json`, `dxdiag.txt` | dxdiag before: DDraw and AGP Not Available, `hardware_acceleration: false` |
| `docs/evidence/dxdiag/before/ddcaps-x64.json`, `ddcaps-x86.json` | HAL caps `DDCAPS_NOHARDWARE`, video-memory surfaces fail `DDERR_NODIRECTDRAWHW`, D3D7 offers RGB Emulation only |
| `docs/evidence/dxdiag/before/getcaps-trace.log` | Every `GetCaps` call from ddcaps and dxdiag; type 11 answered `unsupported` |
| `docs/evidence/dxdiag/before/ddcaps-x64-0012-only.json` | With 0012 only: HAL present, but blit results lost after an emulated blit (the 0013 bug) |
| `docs/evidence/dxdiag/before/console-ddraw-window-0012-only.png` | With 0012 only: the DirectDraw window shows nothing (the 0014 bug) |
| `docs/evidence/dxdiag/after/dxdiag.json`, `dxdiag.txt` | dxdiag after: DDraw, D3D and AGP Enabled, `hardware_acceleration: true`, "No problems found" |
| `docs/evidence/dxdiag/after/ddcaps-x64.json`, `ddcaps-x86.json` | HAL present (`DDCAPS_BLT`, `BLTSTRETCH`, `BLTCOLORFILL`, `3D`, `DDCAPS2_NONLOCALVIDMEM`, ...), surfaces in `DDSCAPS_VIDEOMEMORY \| DDSCAPS_LOCALVIDMEM`, every blit check passes, 130 to 170 windowed blits to the primary without a failure; x86 lists the Direct3D HAL and T&L HAL devices |
| `docs/evidence/dxdiag/after/ddcaps-*.run.json`, `ddcaps-*.modules.json` | ddcaps ran in the user's session 1, and `ddraw.dll` loaded `viogpu_d3d10.dll` |
| `docs/evidence/dxdiag/after/getcaps-trace.log` | Type 11 now answered (`GetCaps D3D7 size=116`). dxdiag also asks for type 14 (`GETDECODEGUIDCOUNT`, DXVA), which stays unsupported and does not affect the result |
| `docs/evidence/dxdiag/after/console-ddraw-window-1.png`, `-2.png` | Console screenshots 6 s apart: the DirectDraw window shows the blitted pattern, moving |
| `docs/evidence/dxdiag/after/probes/` | `Run-AllProbes.ps1` after the fix: all 10 probes (D3D9, D3D11, D3D12, OpenGL, Vulkan; x64 and x86) pass on the VirtIO GPU or Venus with readback OK |

The D3D9 probe ran at 28.7 fps (x64) and 25.7 fps (x86). The e2e worker's
Windows 10 run of the unpatched package measured 29.6 and 29.7 fps
(`docs/evidence/acceptance/windows-10/probes/` on branch `work/e2e`), so the
patches do not slow the D3D9 path.

## Reproduce

The test program is `docs/evidence/dxdiag/ddcaps/ddcaps.c`.
`docs/evidence/dxdiag/ddcaps/build.sh` builds it for x64 and x86 into
`out/ddcaps/` in the shared toolchain image; the build is reproducible:

```
e11dbf5bb4ca3e7c47767e73052176b74c7090fbfa4dc9c5cbe8cfdcd0d14d40  out/ddcaps/x64/ddcaps.exe
5ccc63bd57133597a4a791fd2b4d07776e28d724c0bc9efa1175eb4e8d9df028  out/ddcaps/x86/ddcaps.exe
```

`ddcaps.exe [--seconds N] [--out FILE]` prints one JSON object:

- the HAL and HEL `DDCAPS`, by flag name;
- local and non-local video memory;
- the Direct3D 7 devices;
- a blit test on two 256×256 X8R8G8B8 video-memory surfaces: colour fill,
  copy, 2× stretch, source colour key and left-right mirror, then a `Lock`
  and eight pixel checks;
- with `--seconds N`, N seconds of windowed blits to the primary surface.

It exits with 0 only when the HAL exists, the surfaces are in video memory,
and every check matches. Each step records whether the HAL advertises it
(`"hal"`). Colour key and mirror are not advertised, so DirectDraw emulates
them.

In a guest with the package installed, the guest scripts in `C:\win3d\guest`
and ddcaps in `C:\win3d\ddcaps\{x64,x86}`, run this as an administrator:

```powershell
C:\win3d\Test-DirectDraw.ps1 -OutDir C:\win3d\results\dxdiag -Seconds 10 [-Trace]
```

`docs/evidence/dxdiag/Test-DirectDraw.ps1` runs both ddcaps builds and
dxdiag in the interactive session through `Run-Probe.ps1` and
`Get-Dxdiag.ps1`, then prints a summary. `-Trace` turns on the UMD's D3D9
trace (`D3D10UMD_D3D9_TRACE=1` in `C:\ProgramData\Yttrium\yttrium.ini`) for
the run and copies `yttrium-user.log` into the output directory.

## Not covered

- Fullscreen exclusive DirectDraw (`DDSCL_EXCLUSIVE`, `Flip`) was not
  tested. ddcaps covers windowed mode only.
- The UMD still advertises no hardware colour-key or mirror blits.
  `D3D9Blt` rejects them, so DirectDraw emulates them on the CPU, which
  0013 makes correct but not fast.
- Windowed blits to the primary reached 13 to 17 per second while ddcaps also
  slept 16 ms per frame. Each present is synchronous. That is enough for a
  2D program to show, but it was not measured further.
- Windows 10 was not run in this slice.
