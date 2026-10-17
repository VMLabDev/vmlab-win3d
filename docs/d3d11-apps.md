# D3D11 applications: the DirectX SDK samples

In the e2e stretch run (`docs/evidence/acceptance/windows-11/apps/`), two
unmodified DirectX SDK (June 2010) D3D11 samples loaded `d3d11.dll`,
`dxgi.dll` and `viogpu_d3d10.dll` but never showed a rendering window:

- **SubD11** showed DXUT's "non-zero reference count" dialog.
- **BasicHLSL11** showed nothing.

The same thing happened on Windows 11 and Windows 10. The D3D9 samples, Blender
and our own D3D11 probe rendered.

There were two separate causes:

| Sample | Cause | Owner | Fix |
| --- | --- | --- | --- |
| SubD11 | `CreateHullShader` failed in the UMD's shader translation, and the runtime removed the device. Two more UMD bugs then showed up: wrong colours and a missing wireframe overlay. | Our UMD | `patches/mesa/0015` to `0017` |
| BasicHLSL11 | DXUT (June 2010) moves its window to (32758, −32768), off the screen, on any Windows 8 or later system without the SDK's `D3D11Ref.dll`. | The application (DXUT) | Not fixable in the UMD or KMD; see [BasicHLSL11](#basichlsl11-a-dxut-bug-on-windows-8-and-later) |

With the fixes, the package was rebuilt (`scripts/build.sh`, commit
`5fd206d`) and installed in a fresh Windows 11 lab with
`scripts/install-into-vm.sh`. Results in that lab:

- SubD11 renders on the hardware device at about 50 fps, and the output
  matches the D3D11 reference rasterizer.
- BasicHLSL11 renders on the hardware device at about 50 fps. Its window is
  off the screen until something moves it, or until `D3D11Ref.dll` is
  present.
- `Run-AllProbes.ps1` passes 10 of 10, plus the 4 WARP runs.
- dxdiag reports hardware acceleration.

The evidence is in `docs/evidence/d3d11-apps/`, listed in
[Evidence](#evidence).

## SubD11: three UMD bugs

### How the failing call was found

1. `probes/dxut-enum` replays DXUT's D3D11 enumeration and device creation:
   - adapters, outputs and display modes, with `FindClosestMatchingMode`;
   - the HARDWARE, WARP and REFERENCE devices at every feature level;
   - `CheckFeatureSupport`, `CheckFormatSupport`, and one device per format
     for `CheckMultisampleQualityLevels`;
   - a DXUT-style swap chain with `Present` and `ResizeBuffers`.

   It prints each HRESULT. On our device every call succeeds
   (`dxut-enum-x64-hw.txt`), so the hypothesis about DXGI output and mode
   enumeration was wrong.
2. Under cdb (WinDbg 1.2610.1001.0, pinned in `environment.txt`), a
   breakpoint on each `ID3D11Device::Create*` method showed the sequence that
   fails. `CreateHullShader` is followed by `D3D11: Removing Device.` and then
   `DXGI_ERROR_DEVICE_REMOVED` (`analysis/cdb-SubD11-device-calls.txt`).
   After the reference-count dialog, DXUT reports "An error occurred in the
   device create callback function"
   (`analysis/console-SubD11-before-device-create-callback-error.png`).
3. The UMD writes warnings to `C:\ProgramData\Yttrium\yttrium-errors.log`.
   That file is created by `dwm.exe`, so the sample's process can only append
   to it after an `icacls ... /grant Users:(M)`. The log named the
   translation step that failed (`analysis/yttrium-errors-SubD11-before-fix.txt`):

   ```
   shader TGSI tessellation translation unsupported ... reason=invalid-control-point-input-declaration
   d3d10umd: pfnSetErrorCb hr=0x80070057 caller=InitD3D11TessellationShader
   ```

   A Create DDI that reports `E_INVALIDARG` through `pfnSetErrorCb` makes the
   D3D11 runtime remove the device.

The hull shaders, disassembled with the sample's own `D3DCompiler_43`, are in
`analysis/SubD11-SubDToBezierHS*-D3DCompiler_43.asm.txt`.

### 0015: hull shader `vPrim`, indexed patch constants, indexed control points

Three constructs in SubD11's hull shaders were rejected:

| Construct | Example | What the translation did |
| --- | --- | --- |
| `SV_PrimitiveID` input | `dcl_input vPrim` | Sent it to the control-point path, which needs a two-dimensional operand |
| Index range over ordinary patch constants in a fork phase | `dcl_indexrange o6.x 8`, `mov o[r0.z + 6].x, ...` | Accepted output ranges over tessellation factors only. D3DCompiler_43 also emits `dcl_indexrange o14.x 6` with only o14 and o17–o19 declared in that phase. |
| Index range over input control points | `dcl_indexrange vicp[32][1].x 2`, `vicp[r0.y + 0][r0.x + 1].x` | Accepted one-dimensional input ranges only. For a relative register index, it applied the vertex index to the selected temporary instead of to each candidate input. |

The patch makes these changes:

- It declares `TGSI_SEMANTIC_PRIMID` once per shader.
- It writes indexed patch constants through the existing select-and-store
  sequence for tessellation factors. Each store keeps its own component mask,
  and the patch declares any register in a range that the phase did not.
- It reads every candidate of an indexed control-point range at the same
  vertex index.

The vertex-index change also applies to geometry shaders that read
`v[n][r + i]`; no geometry shader was tested.

### 0016: `icb[r + n]` lost its offset

Two `static const` arrays in SubD11's `SmoothPS` share one immediate constant
buffer:

- four light directions at `icb[0..3]`;
- four light colours at `icb[4..7]`.

The light loop reads `icb[r2.w + 4]`
(`analysis/SubD11-SmoothPS-ps_5_0-D3DCompiler_43.asm.txt`). The translation
added the 4 to a copy of the register, then built the indirect source from
the unmodified one. Every light colour was therefore read as a light
direction, and the character rendered purple and speckled
(`analysis/console-SubD11-after-0015-before-0016.png`). The bug is in
`translate_src_operand` and affects any D3D10 or D3D11 shader that indexes a
`static const` array placed after another one.

`probes/d3d11-tess` value 17 runs the same loop. It returned the direction
values before the fix (`analysis/tess-x64-hw-before-0016.txt`) and matches
after it.

### 0017: `D3D11_FILL_WIREFRAME`

The venus2 pipeline always used `VK_POLYGON_MODE_FILL` and never enabled
`fillModeNonSolid`. SubD11's wire overlay redraws the mesh with a wireframe
rasterizer state and `LESS` depth. Drawn as filled triangles at equal depth,
it failed the depth test, so no wires appeared.

The fill mode now travels in `yttrium_venus_draw_state` and in the pipeline
key, and `polygonMode` uses it when the host device has `fillModeNonSolid`
(RADV does). The REFERENCE device on the same VM draws the same green wires
(`analysis/console-SubD11-REFERENCE-device.png`).

### The tessellation probe

`tess_probe.exe` (source `probes/d3d11-tess/`) builds a 4-patch hull/domain pipeline from
HLSL compiled at run time. Its patch-constant function uses each construct
above: indexed fork-phase outputs, relative `vicp` reads, `SV_PrimitiveID`,
and a constant buffer indexed by the primitive ID. It also runs the `icb`
light loop. The probe writes 18 values per patch to an
`R32G32B32A32_FLOAT` target, reads them back and compares them with values
computed on the CPU.

After the fixes, all 72 values match on the hardware device and on WARP, x64
and x86 (`tess-{x64,x86}-{hw,warp}.txt`). Before 0015 the hull shader
removed the device. Before 0016 the four `icb` values were wrong.

## BasicHLSL11: a DXUT bug on Windows 8 and later

The failure chain is in DXUT's own code (`DXUT11/Core/DXUTDevice11.cpp` and
`DXUT.cpp` in the June 2010 SDK):

1. **Each device type replaces the adapter.**
   `CD3D11Enumeration::EnumerateDevices` creates a HARDWARE, a WARP and a
   REFERENCE device on the default adapter. After each one that succeeds, it
   replaces the adapter it is enumerating with that device's adapter
   (`IDXGIDevice1::GetAdapter`).
2. **The WARP adapter has no outputs.** Since Windows 8, WARP runs on the
   render-only "Microsoft Basic Render Driver" adapter. Microsoft's
   `IDXGIAdapter::EnumOutputs` documentation says: "If the adapter came from a
   device created using D3D_DRIVER_TYPE_WARP, then the adapter has no
   outputs, so DXGI_ERROR_NOT_FOUND is returned." The REFERENCE device needs
   `D3D11Ref.dll`, which only the full DirectX SDK installs. Without it, WARP
   is the last device that succeeds, and DXUT is left with an adapter that has
   no outputs. `dxut-enum-x64-hw.txt` shows the WARP device's adapter
   returning `0x887a0002` from `EnumOutputs(0)`, and
   `analysis/dxut-enum-enum-ref.txt` shows the REFERENCE device's adapter
   returning the desktop output.
3. **DXUT drops the adapter.** `EnumerateOutputs` finds no outputs, so DXUT
   drops our adapter and falls back to its "no adapter" list. It still
   creates a HARDWARE device, which is ours, and renders with it. The sample's
   status line shows `HARDWARE:` with an empty adapter name
   (`console-d3d11-x64-BasicHLSL11-moved-on-screen.png`).
4. **DXUT places the window from uninitialised memory.** In that list there is
   no output, so `DXUTGetMonitorFromAdapter` returns `NULL`, and
   `GetMonitorInfoW(NULL)` fails. `DXUTChangeDevice` then centres the window
   in the uninitialised `MONITORINFO` on its stack. cdb shows
   `GetMonitorInfoW hmon=0000000000000000` twice and then
   `SetWindowPos ... x=32758 y=-2147483648 cx=-32758 cy=0`
   (`analysis/cdb-BasicHLSL11-window-placement.txt`). Windows clamps the
   window to (32758, −32768), 200×200 (`windows-BasicHLSL11-asis.txt`).

None of these steps calls into the UMD or the KMD, and the WARP adapter belongs
to Windows. The fallback runs on every Windows 8 or later system where
`D3D11Ref.dll` is absent. Where the window lands depends on what is left on
the stack:

- BasicHLSL11 ends up off the screen.
- SubD11 ends up at (0, 0) in DXUT's initial 200×200 size, still rendering
  (`console-d3d11-x64-SubD11.png`, `windows-SubD11-asis.txt`).

Current DXUT (github.com/microsoft/DXUT) no longer replaces the adapter in
`EnumerateDevices`.

The rendering itself works on our hardware device in every case:

| Condition | BasicHLSL11 | SubD11 |
| --- | --- | --- |
| As installed by the e2e (DXSETUP runtime only) | Off the screen, rendering. After an outside `SetWindowPos` (`analysis/movewin.ps1`; the application is unchanged): 800×600 on HARDWARE, about 46 fps (`console-d3d11-x64-BasicHLSL11-moved-on-screen.png`) | 200×200 at (0, 0), rendering on HARDWARE (`console-d3d11-x64-SubD11.png`) |
| `D3D11Ref.dll` from `DXSDK/Developer Runtime/x64` in `System32`, as the SDK installer puts it | 800×600 centred, HARDWARE "Hardsoft VirtIO GPU 3D controller", about 52 fps (`console-d3d11-x64-BasicHLSL11-with-D3D11Ref.png`) | 640×480 with materials and wires, HARDWARE, about 47 fps (`console-d3d11-x64-SubD11-with-D3D11Ref.png`) |

`D3D11Ref.dll` is only the SDK's REFERENCE rasterizer. With it present, the
samples still render on our hardware device; DXUT only enumerates the REF
device. `D3D11Ref.dll` is not redistributable, so the package cannot ship it.
To see these samples on the console, `tests/e2e/apps.sh` has two options:

- run the DirectX SDK's own installer (its developer runtime puts
  `D3D11Ref.dll` in `System32`);
- move the window after start-up.

`tests/e2e/apps.sh` now takes the first option and installs `D3D11Ref.dll`
from the pinned SDK (commit `3bf8e4c`). It has not been re-run since.

## Not covered

- Windows 10 was not re-run after the fix. The UMD changes are the same DLL
  on both, and the DXUT behaviour follows from Windows 8+ WARP, so Windows 10
  is expected to match.
- The samples ran only as x64, as in the e2e run. The probes ran as x64 and
  x86.
- Dynamic indexing of patch constants in a domain shader
  (`dcl_indexrange vpc6.xyz 4`) is still rejected. SubD11 does not use it.
  The first version of `tess_probe` did, and it failed with
  `dcl_input_index_range` unsupported.

## Evidence

`docs/evidence/d3d11-apps/`, from the fresh lab with the package built at
`5fd206d` (`environment.txt` has the pins and hashes):

| File | What it shows |
| --- | --- |
| `console-d3d11-x64-SubD11.png`, `windows-SubD11-asis.txt` | SubD11 as installed: rendering on HARDWARE in DXUT's 200×200 window |
| `console-d3d11-x64-BasicHLSL11-offscreen.png`, `windows-BasicHLSL11-asis.txt` | BasicHLSL11 as installed: window at (32758, −32768) |
| `console-d3d11-x64-BasicHLSL11-moved-on-screen.png`, `movewin-BasicHLSL11.txt` | The same process after an outside `SetWindowPos`: rendering on HARDWARE |
| `console-d3d11-x64-*-with-D3D11Ref.png`, `windows-*-with-D3D11Ref.txt` | Both samples with the SDK's `D3D11Ref.dll` in `System32` |
| `d3d11-x64-*.run.json`, `*.modules.txt` | `Run-Probe.ps1 -Raw` runs as the autologon user: system `d3d11.dll` and `viogpu_d3d10.dll`, no app-local modules |
| `tess-{x64,x86}-{hw,warp}.txt` | Tessellation probe: 72 of 72 values match on each |
| `dxut-enum-{x64,x86}-{hw,warp}.txt` | DXUT enumeration replay: every call succeeds; the WARP device's adapter has no outputs |
| `run-allprobes.txt`, `run-allprobes-summary.json` | `Run-AllProbes.ps1 -Warp`: 10 of 10 hardware runs pass, plus 4 WARP runs |
| `dxdiag.json` | `Get-Dxdiag.ps1 -AsUser`: DirectDraw, Direct3D and AGP acceleration enabled; `hardware_acceleration: true` |
| `console-d3d9-x64-BasicHLSL-regression.png` | The D3D9 BasicHLSL sample still renders |
| `analysis/` | The investigation lab, before the fixes: cdb logs and their scripts, UMD error log lines, disassembly, before and after screenshots, REFERENCE-device screenshot |

To rebuild the probes, run `probes/dxut-enum/build.sh` and
`probes/d3d11-tess/build.sh`. They write to `out/probes-extra/{x64,x86}/`,
not to `out/probes/`. `Run-AllProbes.ps1` runs every `*_probe.exe` in
`out/probes/`, and these two are diagnostics, not acceptance probes.
