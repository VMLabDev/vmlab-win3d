# Index ranges ds.md left open, and hull shader join phases

[`ds.md`](ds.md) fixed domain shaders that index patch constants and left
three questions open: vertex shader input ranges, tessellation-factor ranges
in hull and domain shaders, and output ranges outside a hull shader's patch
phases. This page answers all three with one repro. It also covers a defect
the repro found on the way: every hull shader whose join phase reads what its
fork phases wrote removed the device, even with immediate indices. The fixes
are `patches/mesa/0031` and `0032`. Investigated on 2026-10-18 and
2026-10-19 on helios: NVIDIA 610.57.04 on `Nvidia1`, RADV (Mesa 26.2.2) on
`AMD1`, vmlab 0.28.0-alpha, QEMU 11.1.1 (the egl-headless patched build for
NVIDIA), virglrenderer 1.3.0. Evidence is in `docs/evidence/shaders2/`.

## Result

| Case | Construct | How it is built | Before | After |
| --- | --- | --- | --- | --- |
| `vs-in` | Vertex shader reads `v[r0.x + 2]` over input-assembler elements, `dcl_indexrange v2.xyzw 4` | d3dcompiler_47 | Matches | Matches |
| `hs-tf` | Hull shader fork phases write `o[r0.y + 0].x` and `o[r0.x + 4].x`, `dcl_indexrange o0.x 4` and `o4.x 2` (tessellation factors) | d3dcompiler_47 | Matches | Matches |
| `hs-join` | Hull shader join phase reads the factors `vpc0.x` to `vpc3.x` with immediate indices | d3dcompiler_47 | Device removed | Matches |
| `hs-join-tf` | Join phase reads `vpc[r0.x + 0].x` and `vpc[r0.y + 4].x`, `dcl_indexrange vpc0.x 4` and `vpc4.x 2` | assembled | Device removed | Matches |
| `ds-tf` | Domain shader reads `vpc[r0.x + 0].x` and `vpc[r0.y + 4].x` over the factors | assembled | Matches | Matches |
| `vs-out` | Vertex shader writes `o[r0.x + 1]`, `dcl_indexrange o1.xyzw 4` | assembled | Device removed | Matches |
| `hs-cp-out` | Hull shader control-point phase writes `o[r0.x + 2]`, `dcl_indexrange o2.xyzw 4` | assembled | Device removed | Matches |
| `ds-out` | Domain shader writes `o[r0.x + 1]` | assembled | Device removed | Matches |
| `gs-out` | Geometry shader writes `o[r0.x + 1]` before each `emit_stream` | assembled | Device removed | Matches |
| `ps-out` | Pixel shader writes `o[r0.x + 0]` over `SV_Target0` to `3`, four render targets | assembled | Device removed | Matches |

"Matches" means every read-back value equals the CPU's and the WARP run's,
for x64 and x86. "Assembled" means d3dcompiler_47 refuses to emit the
construct, so the probe writes the bytecode itself ([The repro](#the-repro)).
WARP runs every assembled shader and gets the CPU's values, so the bytecode
is valid Shader Model 5.

So the three open questions come out as follows:

1. **Vertex shader input ranges already worked.** d3dcompiler_47 does emit
   them (`dcl_indexrange v2.xyzw 4`). They go through the same
   one-dimensional input code as pixel shader inputs.
2. **Tessellation-factor ranges worked in fork phases and domain shaders.**
   In join phases they did not: no join phase that reads its fork phases'
   outputs worked at all, indexed or not. `0032` fixes it.
3. **Output ranges outside the patch phases failed in every stage.** `0031`
   fixes them. d3dcompiler_47 cannot produce them from HLSL: `o.a[idx] = v`
   in a vertex, domain, geometry or pixel shader is `error X3500: array
   reference cannot be used as an l-value; not natively addressable`. Only
   hand-written bytecode or another compiler would emit them, so the probe
   assembles them.

Proof (filled in from the runs below):

| Check | AMD1 (RADV) | Nvidia1 (RTX 5090) |
| --- | --- | --- |
| `d3d11_index2.exe`, 10 cases × 4 `sel` values × 4 quadrants, x64 and x86, before (`patches/mesa` up to `0030`, `0035`, `0040` to `0042`) | not run | 3 of 10 (`before/nvidia1/`) |
| The same with `0031` and `0032` | AMD1-RESULT | NV1-RESULT |
| `tests/e2e/run.sh --template windows-11` | AMD1-E2E | NV1-E2E |

## The repro

`docs/evidence/shaders2/src/d3d11_index2.cpp` follows the ds repro. It draws
one quad or patch into each quadrant of a 64×64 `R32G32B32A32_FLOAT` target
(four targets for `ps-out`). Each shader indexes its array with
`(quadrant + sel) & 3`, or a value derived from it. `sel` comes from a
constant buffer, so no compiler can fold the index. The probe reads back
the centre pixel of each quadrant for `sel` 0 to 3, compares it with the
CPU's value, and prints the disassembly lines with the index ranges and
indexed accesses. Each case gets its own device, so a removed device does not
hide the other cases.

The output cases write a marker, `(p + 1, sel + 1, idx + 1, 1)`, to element
`idx` and zero to the others. The pixel shader that reads them weights the
elements by 1, 10, 100 and 1000, so a store to the wrong register shows up
as the wrong power of ten.

### Assembled shaders

For a construct that d3dcompiler_47 refuses, the probe compiles a skeleton
in HLSL first. The skeleton has the signatures and declarations the case
needs, and it uses `sel` so that it declares the constant buffer. The probe
then:

1. keeps the skeleton's declarations, without its `dcl_temps`;
2. appends its own `dcl_indexrange`, `dcl_temps` and code, encoded token by
   token (`operand()` and `ins()` in the source; the token values are those
   of the WDK's `d3d11TokenizedProgramFormat.hpp`);
3. for a hull shader, replaces only one phase: the control-point phase for
   `hs-cp-out`, the join phase for `hs-join-tf`. The other phases stay as
   d3dcompiler_47 emitted them;
4. puts the code in the `SHEX` chunk and recomputes the DXBC checksum. The
   checksum is MD5 with DXBC's own padding of the last block. The probe
   first checks its implementation against the checksum of the skeleton
   that d3dcompiler_47 wrote, and stops if they differ.

`D3DDisassemble` accepts every result. Its output is in `after/*/asm/`,
for example `vs-out-vs_5_0.asm.txt`:

```
dcl_output o1.xyzw
...
dcl_output o4.xyzw
dcl_indexrange o1.xyzw 4
dcl_temps 2
...
mov o[r0.x + 1].xyzw, r1.xyzw
```

`--asm-only` builds and disassembles every shader without creating a device.
The assembler was developed that way under Wine, with the guest's own
`d3dcompiler_47.dll`.

`docs/evidence/shaders2/src/build.sh` builds x64 and x86 in the toolchain
image. `docs/evidence/shaders2/src/run-repro.sh <lab-dir> <vm> <out-dir>`
runs both architectures, first on the hardware device and then on WARP, in
the autologon user's session. It copies the logs back and diffs every
read-back line of hardware against WARP. It also collects the UMD's warning
log. It exits 0 only when all four runs pass and both diffs are empty.

## Cause and fix: output ranges (`patches/mesa/0031`)

The UMD translates DXBC to TGSI in `ShaderTGSI.c`. A `dcl_indexrange` over
`o#` went to `dcl_tcs_tess_factor_index_range`, which accepts ranges only in
a hull shader's fork and join phases. Everywhere else it reported the range
as unsupported, and the translation failed (`before/nvidia1/yttrium-errors.txt`):

```
unsupported behavior owner=d3d10umd function=dcl_tcs_tess_factor_index_range ... line=1010
shader creation failed owner=d3d10umd stage=vs reason=tgsi_translation_failed
d3d10umd: pfnSetErrorCb hr=0x80004005 caller=CreateVertexShader:1896
```

`Create*Shader` then returned `DXGI_ERROR_DEVICE_REMOVED` (`0x887a0005`),
and `GetDeviceRemovedReason` returned `DXGI_ERROR_DRIVER_INTERNAL_ERROR`
(`0x887a0020`). For a domain shader the failure came at the first draw,
when the UMD binds the hull and domain shader pair.

TGSI cannot express `o[r + 1]` with indirect addressing. The TGSI outputs of
consecutive D3D registers are not consecutive: the position sits among them,
and one register's components can live in different TGSI outputs. The patch
phases already had a way around this, from `0015`. An indexed store writes a
temporary, and then each register of the range is written under
`UIF index == register`. `0031` records output ranges in every stage and
sends their stores through that sequence. Each register is written through
the TGSI output that an immediate store to it would use. A register of the
range that the shader does not declare gets no store. A geometry shader's
packed scalar system values (layer, viewport index, primitive ID) are
written through a separate temporary, so a range over them is still
reported as unsupported.

## Cause and fix: join phases (`patches/mesa/0032`)

d3dcompiler_47 emits a join phase whenever the patch-constant function
computes one output from another. In `hs-join`, `o.c.x = o.e[idx]` becomes:

```
hs_join_phase
dcl_input vpc0.x
...
dcl_input vpc3.x
dcl_output o6.x
mov r1.x, vpc0.x
mov r1.y, vpc1.x
mov r1.z, vpc2.x
mov r1.w, vpc3.x
dp4 o6.x, r1.xyzw, icb[r0.x + 0].xyzw
```

The translation keeps such `vpcN` reads in TGSI's output file, and they
failed in three places:

1. **No NIR for the hull shader.** Yttrium's TGSI-to-NIR preflight refused
   every output read outside a pixel shader:

   ```
   shader_compile_nir skipped stage=tcs ... reason=unsupported_tgsi_output_read ... file=OUT(3)
   shader_state_create failed owner=yttrium stage=tcs ... reason=nir_unavailable
   d3d10umd: pfnSetErrorCb hr=0x80004005 caller=InitD3D11TessellationShader:3168
   ```

   `0032` allows direct reads of a hull shader's patch outputs (`PATCH`,
   `TESSOUTER`, `TESSINNER`) in `yttrium_shader.c`. Yttrium's
   `tgsi_to_nir` already stores those outputs directly, so other invocations
   can read them, and its read path already loads them.
2. **One factor instead of the vector.** `tgsi_to_nir` read a
   tessellation-level output as element 0 of its scalar array. `0032`
   rebuilds the packed vector before the TGSI swizzle applies, as the TES
   input path already did.
3. **The wrong component.** `ShaderTGSI.c` mapped `vpcN` to the whole
   `TESSOUTER` or `TESSINNER` register, so `vpc1.x` read `TESSOUTER.x`,
   which is factor 0. The component of each factor was known only within
   the phase that declared it. `0032` keeps it across phases
   (`tcs_tess_factor_component_mask`) and selects it, for immediate reads
   and for the indexed reads that `0030`'s selection code makes
   (`hs-join-tf`).

Fixes 2 and 3 cannot be seen without fix 1, and fix 3 is what makes `hs-join`
match rather than return factor 0 for every element.

## What remains

- A `dcl_indexrange` over a geometry shader's packed scalar system values is
  still rejected. A compiler would not emit one; layer and viewport index are
  scalars.
- A domain shader range over tessellation factors that the shader does not
  declare would declare the missing registers as ordinary patch constants.
  d3dcompiler_47 cannot emit such a range, and the repro declares every factor.
- Indexed reads of patch constants in a join phase
  (`vpc[r0.x + 6]` over ordinary constants) use the same code as
  `hs-join-tf`, but were not run.
- Windows 10 was not run; the UMD is the same DLL.

## Evidence

All under `docs/evidence/shaders2/`:

| Path | What it is |
| --- | --- |
| `src/d3d11_index2.cpp`, `src/build.sh`, `src/run-repro.sh` | The repro, its build and its runner |
| `before/nvidia1/` | Run with the package built at `82d8e51` (`patches/mesa` up to `0030`, `0035`, `0040` to `0042`): logs per architecture and device, hardware-against-WARP diffs, `yttrium-errors.txt`, `asm/` |
| `after/{amd1,nvidia1}/` | The same with `0031` and `0032`, run in the e2e labs after the e2e run (kept with `--keep`, then destroyed); `asm/` holds the disassembly of each case's indexed or assembled shader |
| `after/{amd1,nvidia1}/e2e-*.txt`, `qemu-argv-env.txt` | The e2e run's host stack, build and results, and QEMU's command line (copied from `docs/evidence/acceptance/`, which was restored to its committed state) |
| `after/nvidia1/kernel-nvrm.txt` | Host kernel `NVRM`/Xid lines for `0000:01:00.0` during the runs |
