# Black textures on NVIDIA and fixed-function texturing

With `patches/mesa/0022`, the DirectX SDK (June 2010) D3D9 samples ran on the
RTX 5090, but their textured meshes were black silhouettes
([`xid69.md`](xid69.md#open-items)). A fixed-function texture probe also
failed its draw with `E_FAIL`, on NVIDIA and AMD alike. This page names the
cause of each, the fix, and the evidence. Investigated on 2026-10-18 on
helios: NVIDIA 610.57.04, RADV (Mesa 26.2.2), vmlab 0.28.0-alpha, QEMU 11.1.1
with the egl-headless patch, virglrenderer 1.3.0. Evidence is in
`docs/evidence/nvtex/`.

## Result

| Issue | Cause | Fix | GPUs affected |
| --- | --- | --- | --- |
| Mipmapped textures black on NVIDIA | Every D3D9 colour texture outside the system-memory pools is a "shared colour" image, which the winsys makes LINEAR. NVIDIA allows LINEAR images with one mip level and one array layer only, so image creation fails | `patches/mesa/0023`: use OPTIMAL tiling when the host refuses LINEAR for the exact create-info | NVIDIA |
| Pre-transformed fixed-function draws fail with `E_FAIL` | For `XYZRHW` vertices the runtime sends its fixed-function pixel shader and no vertex shader. The UMD built one for only two vertex layouts | `patches/mesa/0024`: build the vertex shader from the declaration for any layout | All |
| R5G6B5 textures black on NVIDIA, red and blue swapped on AMD | D3D `R5G6B5` and `A1R5G5B5` mapped to the wrong Vulkan `PACK16` formats. NVIDIA does not support the format chosen for R5G6B5 at all | `patches/mesa/0025`: map them to `R5G6B5_UNORM_PACK16` and `A1R5G5B5_UNORM_PACK16` | NVIDIA (black), AMD (swapped) |

All three are proven on `Nvidia1` and `AMD1` with a package built at commit
`e428846` (`patches/mesa` 0001 to 0025):

| Check | Nvidia1 (RTX 5090) | AMD1 (RADV) |
| --- | --- | --- |
| `d3d9_tex.exe matrix`, pixel readback, 27 cases | 27 of 27 pass, x64 and x86 | 27 of 27 pass, x64 and x86 |
| Same, before the fix (0001 to 0022) | 4 of 22 pass | 19 of 22 pass |
| `tests/e2e/run.sh --template windows-11` | 12 of 12 | 12 of 12 |
| `Run-AllProbes.ps1` | 10 of 10, exit 0 | 10 of 10, exit 0 |
| BasicHLSL x64 and x86, ParallaxOcclusionMapping x64 | Textured: the `tiny` model and the stone disc (`console-*.png`). Each ran its full 30 s or 25 s, then was closed. POM showed 28.75 fps at 800×600 with vsync off | Textured, as before. POM showed 36.26 fps |
| Host Xid during the runs | none (`kernel-nvrm-*.txt` are empty) | not applicable |

## The repro

`docs/evidence/nvtex/src/d3d9_tex.cpp` draws one quad with a 256×256
texture. Each mip level is a solid colour: level 0 red, 1 green, 2 blue, 3
yellow, and so on. The probe reads back the centre pixel with
`GetRenderTargetData`, so the colour names the level that was sampled.
Black means nothing was sampled. A key=value argument changes one thing at a
time:

- the path: `hlsl` and `hlsl3` (vs/ps 2.0 and 3.0, compiled at run time by
  the system's `d3dcompiler_47.dll`), and fixed function, either
  pre-transformed (`ff-rhw`, `ff-rhw-mod`, `ff-rhw-diffuse`) or transformed
  (`ff-xyz`, `ff-xyz-lit`);
- the format (`x8`, `a8`, `dxt1`, `r5g6b5`, `a1r5g5b5`), the pool
  (`managed`, `default` through `UpdateTexture`, `dynamic`) and the level
  count;
- the sampler state: `MIPFILTER`, `MAXMIPLEVEL` and `MIPMAPLODBIAS`;
- a D24X8 depth buffer, which routes the draw through the 0022 shadow pass.

`matrix` runs a fixed list of 27 cases, each on its own device. Build it with
`docs/evidence/nvtex/src/build.sh`.

Before the fix, the matrix gave a clear pattern
(`before/*/d3d9_tex-x64.txt`):

| Case | Nvidia1 | AMD1 |
| --- | --- | --- |
| X8R8G8B8 or A8R8G8B8, 9 levels, MANAGED, any filter, bias or depth | black | pass |
| The same with 1 level | pass | pass |
| DXT1, 9 levels | pass | pass |
| X8R8G8B8, 9 levels, DEFAULT or DYNAMIC pool | `CreateTexture` fails with `E_OUTOFMEMORY` | pass |
| R5G6B5, 9 levels | black | the level-2 colour, blue, reads as red |
| Fixed function, `XYZRHW\|TEX1`, 9 levels | `Draw` fails with `E_FAIL` | pass |
| Fixed function, `XYZRHW\|DIFFUSE\|TEX1` | `Draw` fails with `E_FAIL` | `Draw` fails with `E_FAIL` |
| Fixed function, `XYZ\|TEX1` and lit `XYZ\|NORMAL\|TEX1`, 9 levels | black | pass |

The mip filter, `MAXMIPLEVEL`, LOD bias, shader model and the depth shadow
made no difference. Only the level count and the format did.

## Mipmapped textures on NVIDIA: `patches/mesa/0023`

### Cause

The D3D9 UMD gives every texture outside the system-memory pools whose
format can be a colour-copy target (X8R8G8B8, A8R8G8B8, R5G6B5, A1R5G5B5 and
others) `PIPE_BIND_RENDER_TARGET | PIPE_BIND_SHARED` (`D3D9PipeBind` in
`D3D9Resource.cpp`). The venus2 winsys creates shared single-sample colour
images with `VK_IMAGE_TILING_LINEAR`, so that importers can reproduce their
layout (`yttrium_venus2_create_texture_image_for_bind`).

Vulkan guarantees LINEAR images only with one mip level and one array layer.
`docs/evidence/nvtex/host-tests/linearmips.c` asks
`vkGetPhysicalDeviceImageFormatProperties2` about the winsys's exact
create-info: 2D, `TRANSFER_SRC | TRANSFER_DST | SAMPLED | COLOR_ATTACHMENT`,
`MUTABLE_FORMAT | ALIAS` (`host-tests/out/`):

| Format, LINEAR | NVIDIA 610.57.04 | RADV |
| --- | --- | --- |
| `B8G8R8A8_UNORM`, `R5G6B5_UNORM_PACK16` and the others listed | maxMipLevels 1, maxArrayLayers 1 | maxMipLevels 15, maxArrayLayers 8192 |
| Any of them, OPTIMAL | maxMipLevels 16, maxArrayLayers 2048 | maxMipLevels 15, maxArrayLayers 8192 |

The winsys checks that query before it creates a sampled image, so on NVIDIA
every mipmapped texture of those formats failed to create:

- In `D3DPOOL_DEFAULT`, the runtime reports the failure:
  `CreateTexture` returns `E_OUTOFMEMORY`.
- In `D3DPOOL_MANAGED`, the application never sees the failure.
  `CreateTexture` and the locks succeed, the shader draws succeed and sample
  black, and the pre-transformed fixed-function draw returns `E_FAIL`.

BasicHLSL's `tiny_skin` is X8R8G8B8 with 9 levels in `D3DPOOL_MANAGED`.
ParallaxOcclusionMapping's textures are mipmapped too. DXT textures are not
colour-copy formats, so they were OPTIMAL and rendered. Single-level
textures stay within NVIDIA's limit, which is why text and UI rendered.

### Fix

`yttrium_venus_shared_colour_tiling()` makes the same query for the exact
create-info: format, type, extent, levels, layers, usage and flags. It returns
LINEAR when the host accepts it and OPTIMAL when it does not. Both the creator
and the importer (`yttrium_venus_import_texture_image_for_bind`) decide
through it, from the same inputs, so they still agree on the layout of a
resource opened in another process. Nothing reads these images as linear
memory: their row pitch is not recorded (`image_row_pitch` is 0). So OPTIMAL
needs no other change. The repro's DEFAULT, DYNAMIC and MANAGED cases pass
with it.

Single-level 2D shared images, among them every back buffer and DXGI shared
surface, stay LINEAR wherever the host accepts them, as both NVIDIA and RADV
do. On RADV every shared image stays LINEAR. ANV was not run.
The decision comes from the host's answer, not from a vendor check.

## Fixed-function texturing: `patches/mesa/0024`

### Cause

The UMD implements the D3D9 fixed-function pipeline with the shaders that
the Windows runtime generates (they carry a `$FF` comment). For transformed
vertices (`XYZ`), the runtime sends both a vertex and a pixel shader, and
those draws worked. For pre-transformed vertices (`XYZRHW`, a `POSITIONT`
declaration), it sends only the pixel shader, and the UMD must supply the
vertex shader. `D3D9DrawDiffuse` did so for only two declarations:

- `POSITIONT` with `COLOR0` and no texture bound;
- `POSITIONT` with `TEXCOORD0`, and exactly one 2D texture.

Any other pre-transformed fixed-function draw failed with `E_FAIL`. The trace
for `XYZRHW|DIFFUSE|TEX1` shows a three-element declaration, one pixel shader
and the warning `unsupported D3D9 draw without a translated VS/PS pair`
(`before/amd1/d3d9-trace-ff-rhw-diffuse.txt`). That vertex format is the
usual one for 2D sprites, HUDs and older games, so this blocked much of what
fixed function is used for. Other examples are a vertex with a specular
colour or two texture sets (a light map).

### Fix

The vertex shader for such a draw is now built from the declaration. Its
inputs are `POSITIONT`, `COLOR0`, and every colour and texture-coordinate
element. `POSITIONT` becomes the clip-space position, as before, and the
other inputs are passed through. The runtime's pixel shader is ps_2_0. Its
inputs are fixed registers (`v0`, `v1`, `t0` to `t7`), not declared usages,
so the declaration, not the shader, says what to feed. A declaration without
a diffuse colour gives opaque white, as fixed-function vertex processing
does, not the zero default input. `ff-rhw-mod` (texture × diffuse with no
diffuse in the vertex) checks that.

### What fixed function still lacks

The probe covers the vertex formats above, one texture stage, and lighting
with one directional light. These are not covered and not tested:

- several texture stages blended together, texture transforms and texture
  coordinate generation, fog, and point sprites with fixed function;
- `XYZRHW` vertices with `PSIZE`. The generated vertex shader passes only
  colours and texture coordinates;
- any fixed-function state the runtime's generated shaders do not express.
  The UMD relies on those shaders.

## 16-bit packed formats: `patches/mesa/0025`

Gallium names packed formats from the least significant bit, and Vulkan
names its `PACK16` formats from the most significant one. D3D `R5G6B5` is
`PIPE_FORMAT_B5G6R5_UNORM`, blue in bits 0 to 4, and the winsys mapped it to
`VK_FORMAT_B5G6R5_UNORM_PACK16`, which has blue in bits 11 to 15. D3D
`A1R5G5B5` went to `VK_FORMAT_B5G5R5A1_UNORM_PACK16` in the same way. On RADV
these textures and render targets had red and blue swapped. NVIDIA does not
support `B5G6R5_UNORM_PACK16` at all (`linearmips` result -11,
`VK_ERROR_FORMAT_NOT_SUPPORTED`), so R5G6B5 textures sampled black. They now
map to `VK_FORMAT_R5G6B5_UNORM_PACK16` and
`VK_FORMAT_A1R5G5B5_UNORM_PACK16`. Both are core formats, and both drivers
support them. The `a1r5g5b5` repro case was added with the fix, so the
A1R5G5B5 swap before it follows from the format definitions and was not
run.

## Proof

Both labs were fresh `x86_64/windows-11@26100.1742.5` labs. Each run did
`tests/e2e/run.sh --host-gpu <gpu> --lab win3d-nvtex-<gpu> --keep` with the
bundle built at `e428846` (`after/*/e2e-build.txt`). Then, in the same lab,
it ran `Run-AllProbes.ps1`, the `d3d9_tex` matrix for x64 and x86, and the
samples through `docs/evidence/xid69/run-sample.sh`. The host config was
helios's: `Nvidia1` with the patched QEMU as `qemu_binary`, and no debug
variables. During development the fixed UMD was copied over the installed
one in kept labs. The results in this page are all from clean installs.
After `e428846`, only the wording of the 0023 header changed, not its code.

## Upstream

0023 to 0025 belong in `arehnman/virtio-win-mesa`, with 0020 and 0022. 0025
is a plain mapping bug that affects every host. 0023 follows the Vulkan
specification's limits for LINEAR images, which any driver may apply.

## Evidence index (`docs/evidence/nvtex/`)

| Path | What it shows |
| --- | --- |
| `src/d3d9_tex.cpp`, `src/build.sh` | The guest repro and its build in the project toolchain image |
| `host-tests/linearmips.c`, `host-tests/out/` | The LINEAR and OPTIMAL limits per format, on NVIDIA and RADV, with no VM |
| `before/<gpu>/d3d9_tex-x64.txt` | The matrix with 0001 to 0022 |
| `before/amd1/d3d9-trace-ff-rhw-diffuse.txt` | The D3D9 DDI trace of the failing `XYZRHW\|DIFFUSE\|TEX1` draw |
| `after/<gpu>/d3d9_tex-{x64,x86}.txt`, `console-d3d9_tex-*.png` | The matrix with 0001 to 0025, from a clean install |
| `after/<gpu>/e2e-results.txt`, `e2e-build.txt`, `e2e-host.txt`, `qemu-argv-env.txt` | The `tests/e2e/run.sh` Done checks and the bundle they ran |
| `after/<gpu>/run-allprobes.txt` | `Run-AllProbes.ps1` in the same lab |
| `after/<gpu>/console-*.png`, `run-*.txt`, `kernel-nvrm-*.txt` | The SDK samples on the console, their run summaries and the host NVRM lines |
