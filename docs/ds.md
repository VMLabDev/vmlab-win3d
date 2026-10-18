# Dynamically indexed patch constants in domain shaders

A D3D11 domain shader that indexed its patch constants with a register made
the D3D11 runtime remove the device ([`known-limits.md`](known-limits.md)).
`patches/mesa/0015` had added indexed patch constants and control points for
hull shaders only ([`d3d11-apps.md`](d3d11-apps.md)). This page names the
cause, the fix (`patches/mesa/0030`), what the repro checks for domain,
geometry and pixel shaders, and what remains. Investigated on 2026-10-18 on
helios: RADV (Mesa 26.2.2) on `AMD1`, NVIDIA 610.57.04 on `Nvidia2`, vmlab
0.28.0-alpha, QEMU 11.1.1 (the egl-headless patched build for NVIDIA),
virglrenderer 1.3.0. Evidence is in `docs/evidence/ds/`.

## Result

| Shader construct (d3dcompiler_47) | Before | After |
| --- | --- | --- |
| Domain shader `vpc[r0.x + 6]` with `dcl_indexrange vpc6.xyz 4` (`ds-vpc`, `ds-vpc-loop`) | Device removed (`DXGI_ERROR_DRIVER_INTERNAL_ERROR`) at the first draw | Matches WARP and the CPU |
| Domain shader `vicp[r0.y + 0][r0.x + 2]` with `dcl_indexrange vicp[4][2].xyzw 2` (`ds-vcp`) | Matches | Matches |
| Geometry shader `v[r0.x + 0][r0.y + 2]` with `dcl_indexrange v[3][2].xyzw 4` (`gs-in`) | Matches | Matches |
| Pixel shader `v[r0.x + 2]` with `dcl_indexrange v2.xyzw 4` (`ps-in`) | Matches | Matches |

The control-point, geometry and pixel shader ranges already worked: the
first through `0015`'s per-vertex range code, the other two through the
original one-dimensional input range code. Only patch-constant ranges were
missing.

Proof, with a package built by `tests/e2e/run.sh` at commit `e690c53`
(`patches/mesa` 0001 to 0025 and 0030):

| Check | AMD1 (RADV) | Nvidia2 (RTX 5090) |
| --- | --- | --- |
| `d3d11_index.exe`, 5 cases × 4 index offsets × 4 quadrants, x64 and x86 | 5 of 5, hardware equals WARP | 5 of 5, hardware equals WARP |
| Same, before the fix (0001 to 0025) | 3 of 5: `ds-vpc` and `ds-vpc-loop` remove the device | 3 of 5, the same two |
| `tests/e2e/run.sh --template windows-11` | 12 of 12 (`after/amd1/e2e-results.txt`) | 12 of 12 (`after/nvidia2/e2e-results.txt`) |
| Host Xid during the runs | not applicable | none (`after/nvidia2/kernel-nvrm.txt` is empty) |

## The repro

`docs/evidence/ds/src/d3d11_index.cpp` compiles its shaders at run time with
the system's `d3dcompiler_47.dll` and draws one patch or quad into each
quadrant of a 64×64 `R32G32B32A32_FLOAT` target. Each shader reads an array
element at index `(quadrant + sel) & 3`, where `sel` comes from a constant
buffer, so the compiler cannot fold the index. The probe reads back the
centre pixel of each quadrant for `sel` 0 to 3 and compares it with the value
computed on the CPU. It also prints the disassembly lines that contain the
`dcl_indexrange` and the indexed access, which shows the construct was
produced (`after/*/asm/`). Each case gets its own device, so a removed device
does not hide the other cases.

| Case | Stage and construct |
| --- | --- |
| `ds-vpc` | A tessellated quad. The hull shader writes `float3 c[4]` patch constants (`vpc6` to `vpc9`, after the six quad tessellation factors). The domain shader returns `c[idx]` |
| `ds-vpc-loop` | The same patch constants, summed in a `[loop]` whose bound comes from the constant buffer; the loop counter is the index |
| `ds-vcp` | The domain shader returns `q[idx].a[j]`: a control point and an attribute array, both indexed by registers |
| `gs-in` | No tessellation. The geometry shader reads `t[vertex].a[idx]` from its input triangle, both indices registers |
| `ps-in` | The pixel shader returns its `nointerpolation float4 a[4]` input at `idx` |

`docs/evidence/ds/src/build.sh` builds x64 and x86 in the toolchain image.
`docs/evidence/ds/src/run-repro.sh <lab-dir> <vm> <out-dir>` runs both on the
hardware device and on WARP in the autologon user's session
(`tests/guest/Run-Probe.ps1`), copies the logs back, diffs every read-back
line of hardware against WARP, and collects the UMD's warning log. It exits 0
only when all four runs pass and both diffs are empty.

## Cause

fxc compiles `c.c[idx]` in a domain shader to:

```
dcl_input vpc6.xyz
...
dcl_input vpc9.xyz
dcl_indexrange vpc6.xyz 4
mov o1.xyz, vpc[r0.x + 6].xyzx
```

(`after/amd1/asm/ds-vpc-ds_5_0.asm.txt`). The UMD translates DXBC to TGSI in
`ShaderTGSI.c`. Its `dcl_input_index_range` accepted ranges over `v[]` and,
since `0015`, over `v[n][]` and `vicp[n][]`. A range over the patch-constant
operand type was rejected as unsupported, which failed the translation. The
source-operand code also asserted an immediate index for patch constants.

`CreateDomainShader` still returned `S_OK`: the UMD translates tessellation
shaders when it binds the hull and domain shader pair. At the first draw the
bind failed, and the UMD reported `E_FAIL` through `pfnSetErrorCb`. The UMD's
log for the run (`before/amd1/yttrium-errors.txt`):

```
unsupported behavior owner=d3d10umd function=dcl_input_index_range ... line=944
d3d10umd domain shader translation failed
tessellation shader bind failed owner=d3d10umd ... ds_state=0000000000000000 action=SetError
d3d10umd: pfnSetErrorCb hr=0x80004005 caller=BindD3D11TessellationStates:3245
```

The runtime then removed the device: `Map` returned
`DXGI_ERROR_DEVICE_REMOVED` and `GetDeviceRemovedReason` returned
`DXGI_ERROR_DRIVER_INTERNAL_ERROR` (`before/*/d3d11_index-*-hw.txt`). The
same lines appear on NVIDIA (`before/nvidia2/`); the gap is in the shader
translation, not in a host driver.

## Fix: `patches/mesa/0030`

Patch constants are separate TGSI `PATCH` inputs, one per D3D register.
Tessellation factors are components of `TESSOUTER` and `TESSINNER`. Neither
is a consecutive register file that TGSI indirect addressing could index.
So the patch:

- records a `dcl_indexrange` over `vpc` in its own range list;
- reads `vpc[base + r]` by selecting among the inputs of the range with
  `USEQ` and `UCMP`, as the existing `translate_indexed_input` does for
  `v[base + r]`;
- declares a register of the range that the shader did not declare on first
  use. fxc declared every register in the repro, so that path was not
  exercised.

In a hull shader's join phase, the patch constants are the fork phases'
outputs, as they already were for an immediate index. No join phase was
tested.

## What remains

- **Output index ranges outside a hull shader's patch phases are still not
  translated.** A `dcl_indexrange` over `o#` in a vertex, domain, geometry or
  pixel shader, or in a hull shader's control-point phase, fails the
  translation. d3dcompiler_47 never produces one from HLSL: writing
  `o.v[idx]` in those stages fails to compile with `error X3500: array
  reference cannot be used as an l-value; not natively addressable`. Only
  hand-written or other compilers' bytecode would hit it.
- d3dcompiler_47 also refuses to index tessellation factors in a domain
  shader (`error X4576: Input array signature parameter cannot be indexed
  dynamically`), so a range over `vpc0` to `vpc5` is not produced either.
  The patch's selection would cover it, untested.
- Vertex shader input ranges (`v[r]` over input-assembler elements) use the
  same one-dimensional code as the pixel shader case, but were not run.
- Windows 10 was not run; the UMD is the same DLL.

## Evidence

All under `docs/evidence/ds/`:

| Path | What it is |
| --- | --- |
| `src/d3d11_index.cpp`, `src/build.sh`, `src/run-repro.sh` | The repro, its build and its runner |
| `before/{amd1,nvidia2}/` | Runs with the package at `b4b8c3f` (0001 to 0025): logs per architecture and device, the hardware-against-WARP diffs, `yttrium-errors.txt` |
| `after/{amd1,nvidia2}/` | The same with 0030, run in the e2e labs after the e2e run (kept with `--keep`, then destroyed); `asm/` holds the full disassembly of each case's indexed shader |
| `after/{amd1,nvidia2}/e2e-*.txt`, `qemu-argv-env.txt` | The e2e run's host stack, build and results, and QEMU's command line (copied from `docs/evidence/acceptance/`, which was restored to its committed state) |
| `after/nvidia2/kernel-nvrm.txt` | Host kernel `NVRM`/Xid lines during the runs: none |
