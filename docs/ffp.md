# D3D9 fixed-function pipeline coverage

Older games rely on the D3D9 fixed-function pipeline. Before this page,
one texture stage on pre-transformed vertices was the only part tested
([`nvtex.md`](nvtex.md)). This page tests multi-stage texture blending,
texture coordinate generation and transforms, fog, lighting, alpha test and
blending, point sprites, and transformed and pre-transformed vertices. Each
test runs with both hardware and software vertex processing, and the
results are compared with Microsoft's reference rasterizer. The page names
the four faults found, the fixes (`patches/mesa/0040` to `0042`) and the
evidence. The work was done on 2026-10-18 on helios: NVIDIA 610.57.04 on
`Nvidia2` and RADV (Mesa 26.2.2) on `AMD1`, vmlab 0.28.0-alpha,
virglrenderer 1.3.0. Evidence is in `docs/evidence/ffp/`.

## Result

| Fault | Cause | Fix | GPUs |
| --- | --- | --- | --- |
| Every fixed-function draw with software vertex processing rendered black | The UMD built a POSITIONT vertex shader from the vertex declaration. A second builder then overwrote it, using the pixel shader's input map, which is empty for ps_2_0. Only the position was passed, so colours and texture coordinates were lost | `0040` | All |
| Table fog under a perspective projection was missing | With shader-emulated fixed function the runtime never calls `SetTransform`, so the UMD always assumed an orthographic projection and fogged by screen z | `0041`: choose W fog from the W range the runtime sends through `UpdateWInfo` | All |
| Vertex fog was smeared across primitives | The fog factor was clamped in the vertex shader before interpolation. D3D9 clamps it per pixel | `0041` | All |
| Pre-transformed and software-processed vertices were fully fogged | The generated POSITIONT vertex shader wrote no fog factor | `0041`: take it from the `FOG` element, else the specular alpha, else 0 | All |
| Points larger than one pixel were black or the wrong size | The point-expansion geometry shader dropped colours, other texture coordinates and fog, applied sprite coordinates even with sprites off, ignored w for shader-computed sizes, and never saw `PSIZE` from `XYZRHW` vertices | `0042` | All |

These are the probe's totals: x64 hardware runs against the reference
rasterizer, packages built by `tests/e2e/run.sh`. Before is the package at
`72ba143` (`patches/mesa` 0001 to 0025 and 0030). After is the package at
`1115df0` (adds 0040 to 0042):

| Run | Nvidia2 before | Nvidia2 after | AMD1 before | AMD1 after |
| --- | --- | --- | --- | --- |
| x64, hardware vertex processing, against the reference | 153 of 168 | 166 of 168 | 152 of 168 | 166 of 168 |
| x86, the same | 153 of 168 | 166 of 168 | 152 of 168 | 166 of 168 |
| x64, software vertex processing, against the reference with software vertex processing | 24 of 168 | 168 of 168 | 22 of 168 | 168 of 168 |
| x64, hardware vertex processing, against the reference with software vertex processing | 155 of 168 | 168 of 168 | 154 of 168 | 168 of 168 |

The two cases still failing after the fix are `lt_spot` and
`lt_spot_falloff`, compared with the reference's own vertex processing. They
differ by at most 12/255, on 5 and 2 of the 64 samples, inside the spot
cone. The driver does not cause this. The reference gives the same pixels as
the driver when the runtime's software vertex processing feeds it
(`compare-x64-hw-vs-refswvp.txt`: 168 of 168). Microsoft's own two vertex
pipelines differ in their spotlight falloff, and the hardware path runs the
runtime's shader.

Proof, from fresh `x86_64/windows-11@26100.1742.5` labs:

| Check | Nvidia2 (RTX 5090) | AMD1 (RADV) |
| --- | --- | --- |
| `tests/e2e/run.sh --template windows-11` | 12 of 12 (`after/nvidia2/e2e-results.txt`) | 12 of 12 (`after/amd1/e2e-results.txt`) |
| `Run-AllProbes.ps1` | 10 of 10, exit 0 | 10 of 10, exit 0 |
| DirectX SDK (June 2010) `Text3D`, x64 and x86 (fixed-function lighting, specular, a `MODULATE` stage, D3DX sprite text) | Renders on `HAL (hw vp)` (`console-text3d-*.png`) | The same |
| Host Xid during the SDK sample runs | none (`after/nvidia2/kernel-nvrm.txt` is empty) | not applicable |
| QEMU | `qemu-vmlab-egl`, QEMU 11.1.50 with the vmlab series (`e2e-host.txt`); the before runs used `qemu-11.1.1-vmlab-egl` | QEMU 11.1.1 (Arch) |

The two e2e runs built their package from commit `1115df0` while
`build/mesa/README.md` had an uncommitted edit. That is why the manifest
says `1115df0-dirty`. A later `scripts/build.sh` on a clean tree, at the
docs commit, gave the same sha256 for all 28 package files
(`after/clean-rebuild.txt`).

## The probe

`docs/evidence/ffp/src/d3d9_ffp.cpp` runs 168 cases. Each case gets its own
device and renders into a 64×64 `A8R8G8B8` render target cleared to
`0x80406080`. The probe reads the target back with `GetRenderTargetData`
and logs an 8×8 grid of pixels, sampled at 4 + 8i. Geometry edges sit on
multiples of 8, half-way between samples, so rasterization rules cannot move
a sample across an edge. The same binary runs on the hardware device and on
`D3DDEVTYPE_REF`, using `d3dref9.dll` from the DirectX SDK pinned in
`tests/e2e/PINS`. `--swvp` creates the device with software vertex
processing. `compare.py` compares each sample channel by channel against
the case's tolerance, which is 2 to 5 steps in 8 bits.

| Group | Cases | What they set |
| --- | --- | --- |
| `b_*` blending, each on `XYZRHW` and on `XYZ` vertices | 70 | `SELECTARG1/2`, `MODULATE`, `MODULATE2X/4X`, `ADD`, `ADDSIGNED(2X)`, `SUBTRACT`, `ADDSMOOTH`, `BLEND{TEXTURE,DIFFUSE,FACTOR,CURRENT}ALPHA`, `BLENDTEXTUREALPHAPM`, `MODULATEALPHA_ADDCOLOR`, `MODULATECOLOR_ADDALPHA`, `DOTPRODUCT3`, `LERP`, `MULTIPLYADD`. Arguments `TEXTURE`, `CURRENT`, `DIFFUSE`, `TFACTOR`, `SPECULAR`, `CONSTANT` and `TEMP` (`RESULTARG`), with `COMPLEMENT` and `ALPHAREPLICATE`. 2, 3 and 4 stages, a disabled stage, `SPECULARENABLE`, defaults with no texture and no diffuse |
| `tg_*` texture coordinates | 15 | A 256×256 texture whose red and green are u and v, point-sampled, so the colour reads back the coordinate. `TEXCOORDINDEX` 1, `CAMERASPACEPOSITION`, `CAMERASPACENORMAL`, `CAMERASPACEREFLECTIONVECTOR` (local viewer on and off), `SPHEREMAP`. `TEXTURETRANSFORMFLAGS` `COUNT2`, `COUNT3`/`COUNT4` with `PROJECTED`, and on `XYZRHW` vertices |
| `fv_*`, `ft_*`, `fog_*` fog | 15 | Vertex and table fog, `LINEAR`, `EXP`, `EXP2`, `FOGCOLOR`, `RANGEFOGENABLE`. Orthographic and perspective projections. `XYZRHW` vertices with and without a specular colour |
| `lt_*` lighting | 19 | Directional, point (attenuation, range) and spot lights (theta, phi, falloff), three at once, materials, `SPECULARENABLE`, `LOCALVIEWER`, `NORMALIZENORMALS` under a scaled world matrix, `COLORVERTEX` with the four material sources, a perspective projection |
| `at_*`, `ab_*` alpha test and blending | 26 | `ALPHAFUNC` `GREATER`, `GREATEREQUAL`, `LESS`, `LESSEQUAL`, `NOTEQUAL`, `NEVER` with `ALPHAREF`, plus 19 `SRCBLEND`/`DESTBLEND`/`BLENDOP` combinations: alpha, additive, modulate, premultiplied, `BOTHSRCALPHA`, `BLENDFACTOR`, `SEPARATEALPHABLENDENABLE`, `REVSUBTRACT`, `MIN`, `MAX`, with alpha test and with fog |
| `pt_*` points | 11 | `POINTSIZE`, `POINTSPRITEENABLE`, `PSIZE` in `XYZRHW` and `XYZ` vertices, `POINTSIZE_MIN`/`MAX`, `POINTSCALEENABLE` with `POINTSCALE_A/B/C` under perspective |
| `xf_*` untransformed geometry | 12 | World, view and projection transforms, perspective-correct texturing, near-plane clipping, culling, a viewport, an indexed vertex buffer, a user clip plane, `VERTEXBLEND` with one weight, depth test, flat shading, lines |

`docs/evidence/ffp/table.md` has every case, before and after, on both
GPUs, with hardware and with software vertex processing. Before the fix,
the hardware-vertex-processing failures were the same on both GPUs except
one: `fog_rhw_nospec` passed on NVIDIA by chance, because an unwritten input
happened to read as zero. The failures were all fog (`fv_linear`,
`fv_linear_tex`, `fv_linear_persp`, `ft_linear_persp`, `ft_exp_persp`,
`fog_rhw_spec`, `ab_fog_blend`), all points (`pt_size_rhw`, `pt_size_xyz`,
`pt_size_min`, `pt_psize_rhw`, `pt_scale`, `pt_scale_ab`), and the two
spotlights. Blending, texture coordinates, lighting other than spotlights,
alpha, and transforms already matched. With software vertex processing,
only cases whose colours do not come from the vertex passed (22 to 24).

`run-repro.sh <lab-dir> <vm> <out-dir>` runs six passes in the autologon
user's session (`tests/guest/Run-Probe.ps1`): x64 and x86 on the hardware
device, x64 with software vertex processing, and the same three on the
reference device. It copies `d3dref9.dll` into `System32` and `SysWOW64`
first, then writes the four comparisons. `make-table.py` builds
`table.md`.

## How fixed function reaches the UMD

The UMD does not advertise fixed-function hardware. The D3D9 runtime turns
fixed-function state into shaders, with a `$FF` comment, and sends them
through the shader DDI:

- **Hardware vertex processing, `XYZ` vertices.** The runtime sends a
  vs_2_0 for transform, lighting, texture generation and transforms, and
  vertex fog, plus a ps_2_0 for the texture stages. The transforms reach
  the UMD only as vertex shader constants. `SetTransform` is never called.
- **Hardware vertex processing, `XYZRHW` vertices.** The runtime sends only
  the pixel shader. The UMD generates the vertex shader from the
  declaration (`patches/mesa/0024`).
- **Software vertex processing.** The runtime transforms and lights on the
  CPU. It writes POSITIONT, COLOR0, COLOR1, FOG and the texture coordinates
  into its own vertex buffer, which it draws with `DrawPrimitive2` and only
  the pixel shader. The UMD generates the vertex shader for this case too.

The UMD itself handles whatever comes after the pixel shader in D3D9:
fog for ps_1_x and ps_2_0, alpha test, and point expansion. The Nine shader
translator (`D3D9NineShader.c`) and `D3D9Draw.cpp` do this work. All four
faults were in that part or in the generated vertex shaders, not in the
runtime's shaders.

## Software vertex processing: `patches/mesa/0040`

A software-processed draw matched two of `D3D9DrawDiffuse`'s POSITIONT
builders. `D3D9InitPositionTRuntimePsFixedFunctionVsInfo` (from `0024`)
reads the declaration and filled the vertex shader's inputs.
`D3D9InitPositionTPsInputFixedFunctionVsInfo` reads the pixel shader's input
map, and it ran second into the same object. For ps_2_0 the inputs are
registers, not declared usages, so the map is empty and only POSITIONT was
left. Every software-processed draw then read colour (0,0,0,1) and texture
coordinate 0: 144 of 168 cases failed on NVIDIA and 146 on AMD
(`before/*/compare-x64-swvp.txt`). The trace showed both builders active
(`rtps=1 rp=1`, from a development build with extra trace lines). The pixel-shader-input builder now runs only when the
declaration builder did not.

## Fog: `patches/mesa/0041`

- **W fog.** Table fog uses eye depth when the projection is perspective,
  and the UMD read that from `transforms[D3DTS_PROJECTION]`. A DDI trace of
  `ft_linear_persp` shows no `SetTransform` call at all. It does show
  `UpdateWInfo near=1 far=19.99`, against `near=1 far=1` for the identity
  projection. `D3D9ProjectionUsesZFog` now picks W fog when `WNear != WFar`,
  and still honours a real projection matrix.
- **Per-pixel clamp.** Nine declared the vertex shader's fog output with
  saturate. With `FOGSTART` 0.2 and `FOGEND` 0.8 across a quad whose depth
  runs from 0 to 1, the factor is 1.33 and -0.33 at the vertices. The
  reference clamps after interpolation, so the quad is unfogged up to 20%
  and fully fogged from 80%. The clamped factor gave a ramp across the whole
  quad. The saturate moved to the pixel shader, where the factor is read.
- **Pre-transformed vertices.** The pixel shader's fog stage reads the
  vertex fog factor from `GENERIC 16`. Neither generated POSITIONT vertex
  shader wrote it, so the factor was undefined: zero (full fog) on both
  GPUs for `fog_rhw_spec`. The shaders now write the declaration's `FOG`
  element when it has one, which software vertex processing provides. Next
  they use the specular alpha, which D3D9 defines as the fog factor of an
  `XYZRHW` vertex. Without a specular colour they write 0, which matches the
  reference (`fog_rhw_nospec`).

## Points: `patches/mesa/0042`

Points larger than one pixel go through `D3D9CreatePointPositionGs`, which
expands each point into a quad:

- The geometry shader wrote only the position and two texture coordinates.
  A point coloured by its diffuse came out black (`pt_size_*`). It now passes
  through every varying a ps_2_0 or ps_3_0 shader can read from D3D9 vertex
  processing: COLOR0, COLOR1, the eight texture coordinate sets and the fog
  factor.
- It wrote sprite coordinates to texture coordinates 0 and 1 even with
  `POINTSPRITEENABLE` off. It now writes them to all eight, and only for
  sprites.
- A size computed by the vertex shader (`POINTSCALEENABLE`, `oPts`) was
  added to the clip-space position without the factor w. Under perspective
  the point therefore shrank with distance twice (`pt_scale`).
- A `PSIZE` element in an `XYZRHW` declaration was ignored (`pt_psize_rhw`).
  The generated POSITIONT shader now passes it in the generic slot the Nine
  shaders use. A plain `PSIZE` output there made the draw fail on NVIDIA,
  and the read-back was all zero.

`pt_scale_ab` was changed during the work so that its sizes stay below the
reference's `MaxPointSize` of 64. The UMD reports 8192, so larger points
cannot be compared.

## What remains

- The spotlight difference above belongs to the runtime. It is not a
  driver issue.
- Not tested: vertex blending with more than one weight, indexed vertex
  blending, bump-environment-map stages, `PREMODULATE`, texture coordinate
  generation into cube and volume textures, W-buffering, multisampling,
  wide lines, and devices with `D3DCREATE_MIXED_VERTEXPROCESSING`. Windows
  10 was not run either, although the UMD is the same DLL.
- Table fog relies on the runtime's `UpdateWInfo` to tell a perspective
  projection apart. Every projection tried here reported it correctly.
- Kept labs from the evaluation template shut down an hour after boot. The
  license monitor (`wlms.exe`) does this because the evaluation license is
  expired. A development run on `Nvidia2` hit this. The runs recorded here
  finished within the hour.

## Upstream

0040 to 0042 belong in `arehnman/virtio-win-mesa`, with 0024. The vertex
shader's fog saturate came with the Gallium Nine translator that
`D3D9NineShader.c` is derived from. Whether upstream Nine has the same
fault was not checked.

## Evidence index (`docs/evidence/ffp/`)

| Path | What it shows |
| --- | --- |
| `src/d3d9_ffp.cpp`, `src/build.sh` | The probe and its build in the project toolchain image |
| `src/run-repro.sh`, `src/compare.py`, `src/make-table.py` | The guest runner, the per-sample comparison and the table |
| `table.md` | Every case, before and after, both GPUs, hardware and software vertex processing |
| `before/<gpu>/` | The six runs and four comparisons with the package at `72ba143` (0001 to 0025, 0030), in the e2e labs of that commit. In the `Nvidia2` lab a development UMD had been tried and was put back first; the restored DLLs match the package's sha256 |
| `after/<gpu>/d3d9_ffp-*.txt`, `compare-*.txt` | The same with 0040 to 0042, from a clean install by `tests/e2e/run.sh` |
| `after/<gpu>/e2e-results.txt`, `e2e-build.txt`, `e2e-host.txt`, `qemu-argv-env.txt`, `status-v.txt` | The e2e Done checks, the bundle, the host stack and the QEMU binary and command line |
| `after/clean-rebuild.txt` | A clean-tree rebuild with the same file hashes as the e2e bundles |
| `after/<gpu>/run-allprobes.txt` | `Run-AllProbes.ps1` in the same lab |
| `after/<gpu>/console-text3d-{x64,x86}.png`, `run-text3d-*.json` | The SDK's fixed-function `Text3D` sample on the console |
| `after/<gpu>/yttrium-errors.txt`, `d3dref9-sha256.txt` | The UMD's warnings during the runs, and the reference DLLs used |
| `after/nvidia2/kernel-nvrm.txt` | Host `NVRM` lines during the sample runs: none |
