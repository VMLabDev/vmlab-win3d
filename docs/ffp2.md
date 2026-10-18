# D3D9 fixed function, part 2: vertex blending, bump mapping, cube texgen, mixed devices, Windows 10

[`ffp.md`](ffp.md) tested D3D9 fixed function against Microsoft's reference
rasterizer and left five areas out. This page covers them:

- vertex blending with more than one weight, indexed vertex blending and
  tweening;
- bump environment mapping (`BUMPENVMAP`, `BUMPENVMAPLUMINANCE`);
- texture coordinate generation into cube and volume textures;
- devices created with `D3DCREATE_MIXED_VERTEXPROCESSING` that switch with
  `SetSoftwareVertexProcessing`;
- a Windows 10 run of the whole fixed-function suite.

The new probe found one fault, in the bump-map formats. It was fixed in
`patches/mesa/0043`. A second change, `0044`, corrects a capability the
driver reported too low. Mixed devices, blending, cube and volume texgen and
Windows 10 already matched the reference. The work was done on 2026-10-19
on helios: NVIDIA 610.57.04 on `Nvidia2` and RADV (Mesa 26.2.2) on `AMD1`,
with vmlab 0.28.0-alpha and virglrenderer 1.3.0. The evidence is in
`docs/evidence/ffp2/`.

## Result

| Area | Finding | Change | GPUs |
| --- | --- | --- | --- |
| Bump mapping with luminance | `L6V5U5` and `X8L8V8U8` did not exist on the HAL: `CheckDeviceFormat` refused them and `CreateTexture` failed with `D3DERR_NOTAVAILABLE`, so `BUMPENVMAPLUMINANCE` could not run. The reference has both | `0043`: offer both, stored as `R8G8B8A8_SNORM` and converted on upload | All |
| Indexed vertex blending | Indices 0 to 8 already matched the reference with hardware vertex processing, but the driver reported `MaxVertexBlendMatrixIndex` 0. That tells applications that hardware vertex processing has no indexed blending. Indices 9 and up are not uploaded by the runtime and land on unset constants | `0044`: report 8, the runtime's palette | All |
| `BUMPENVMAP` with `V8U8`, every bump matrix, the bump stage second, texgen into the bump stage | Match | none | All |
| Two and three weights, `0WEIGHTS`, fewer weights used than the vertex has, lit and texgen, perspective, tweening | Match | none | All |
| Cube maps: explicit 3D coordinates (`XYZ` and `XYZRHW`), `COUNT3` transforms, `CAMERASPACEREFLECTIONVECTOR` (local viewer on and off, perspective), `CAMERASPACENORMAL`, `CAMERASPACEPOSITION`. Volume textures | Match | none | All |
| Mixed devices: hardware and software draws in the same scene, from user pointers and from vertex buffers, with fog, texgen, blending, points and 256-matrix indexed blending on the software side; the whole `d3d9_ffp` suite on a mixed device in each mode | Match | none | All |
| Windows 10 (19045) | The same results as Windows 11 for both probes | none | `Nvidia2` |

Totals, x64 hardware runs against the reference, from the packages built by
`tests/e2e/run.sh`. Before is the package at `82d8e51` (`patches/mesa` up
to `0042`). After is the package at `c51dec0` (adds `0043` and `0044`).

| `d3d9_ffp2` (49 cases) | W11 `Nvidia2` before | W11 `Nvidia2` after | W11 `AMD1` before | W11 `AMD1` after | W10 `Nvidia2` before | W10 `Nvidia2` after |
| --- | --- | --- | --- | --- | --- | --- |
| hardware VP against the reference | 43 | 48 | 43 | 48 | 43 | 48 |
| x86, the same | 43 | 47 + 1 rerun | 43 | 48 | 43 | 48 |
| software VP against the reference with software VP | 44 | 49 | 44 | 49 | 44 | 49 |
| mixed device, hardware mode, against the reference | 43 | 48 | 43 | 48 | 43 | 48 |
| mixed device, software mode, against the reference with software VP | 44 | 49 | 44 | 49 | 44 | 48 + 1 rerun |
| hardware VP against the reference with software VP | 44 | 49 | 44 | 49 | 44 | 49 |

| `d3d9_ffp` (168 cases) | W11 `Nvidia2` before | W11 `Nvidia2` after | W11 `AMD1` before | W11 `AMD1` after | W10 `Nvidia2` before | W10 `Nvidia2` after |
| --- | --- | --- | --- | --- | --- | --- |
| hardware VP against the reference | 166 | 166 | - | 166 | 166 | 166 |
| x86, the same | - | 166 | - | 166 | 166 | 166 |
| software VP against the reference with software VP | - | 168 | - | 168 | 168 | 168 |
| mixed device, hardware mode, against the reference | 166 | 166 | 166 | 166 | 166 | 166 |
| mixed device, software mode, against the reference with software VP | 168 | 168 | 168 | 168 | 168 | 168 |
| hardware VP against the reference with software VP | 168 | 168 | - | 168 | 168 | 168 |

A dash is a run that was not made. In the `d3d9_ffp` rows, the two
failures are `lt_spot` and `lt_spot_falloff`. The runtime's two vertex
pipelines differ there, not the driver ([`ffp.md`](ffp.md)).

The one case still failing in `d3d9_ffp2` after the fix is
`vb_indexed_color`, which uses `D3DCOLOR` matrix indices. The reference
device with its own hardware vertex processing loses the device on it
(`GetRenderTargetData` returns `D3DERR_DEVICELOST`). The driver matches the
reference fed by the runtime's software vertex processing, in every run.

Two cases failed once, each in one run. These were `bm_texgen` in the
Windows 11 `Nvidia2` x86 run and `cm_normal` in the Windows 10 `Nvidia2`
mixed software run. `CreateRenderTarget` returned `E_OUTOFMEMORY` after a
Venus ring or fence timeout in the guest
(`after/*/yttrium-errors-d3d9_ffp2.txt`). At the same time the host's
NVIDIA driver logged `dmaAllocMapping_GM107: can't alloc VA space for
mapping` (`NV_ERR_NO_MEMORY`). The other workers' labs were sharing the
card then, and one of them raised Xid 69 at 01:50:44 and at 03:04:13 (see
[What remains](#what-remains)). Each case passed when run again in the
same lab (`after/*/rerun/`). Every other run of these cases passed, and
`AMD1` had no such failure.

Proof, from fresh labs on the `windows-11` (26100.1742.5) and `windows-10`
(19045.2006.8) templates:

| Check | W11 `Nvidia2` | W11 `AMD1` | W10 `Nvidia2` |
| --- | --- | --- | --- |
| `tests/e2e/run.sh` | 12 of 12 (`after/w11-nvidia2/e2e/results.txt`) | 12 of 12 (`after/w11-amd1/e2e/results.txt`) | 12 of 12 (`after/w10-nvidia2/e2e/results.txt`) |
| QEMU | `qemu-vmlab-egl`, QEMU 11.1.50 (`e2e/host.txt`) | QEMU 11.1.1 (Arch) | `qemu-vmlab-egl` |

## The probe

`docs/evidence/ffp2/src/d3d9_ffp2.cpp` uses the same method as `d3d9_ffp`.
Each case gets its own device and renders into a 64×64 `A8R8G8B8` target.
The probe reads back an 8×8 grid of samples and compares them with the
reference rasterizer (`d3dref9.dll` from the pinned DirectX SDK), using
`docs/evidence/ffp/src/compare.py`. New options:

- `--mixed` creates the device with `D3DCREATE_MIXED_VERTEXPROCESSING`.
  It selects hardware processing, or with `--swvp` software processing.
- `d3d9_ffp.cpp` got the same `--mixed` option. Its 168 cases are
  unchanged.

| Group | Cases | What they set |
| --- | --- | --- |
| `vb_*` vertex blending | 18 | A 12×12 grid, larger than the target, warped by up to four world matrices with smoothly varying weights. `0WEIGHTS` to `3WEIGHTS` (`XYZB1` to `XYZB4`), one weight used of three, lit, perspective, and blended camera-space position and normal read back through texgen. Indexed: `INDEXEDVERTEXBLENDENABLE` with `LASTBETA_UBYTE4` indices 0, 3, 5 and 8 for 0 to 3 weights, lit and texgen, and `LASTBETA_D3DCOLOR`. `TWEENING` with `POSITION1` and `NORMAL1`, unlit and lit |
| `bm_*` bump mapping | 12 | A 16×16 bump map, constant or varying, perturbs the coordinates of a gradient whose colour reads back the coordinate. `BUMPENVMAP` on `XYZ` and `XYZRHW` vertices, a full 2×2 bump matrix, `BUMPENVMAPLUMINANCE` with `LSCALE` and `LOFFSET` on `X8L8V8U8` and `L6V5U5`, the bump stage as stage 1 after a base texture, and texgen coordinates into the bumped stage |
| `cm_*`, `vol_*` cube and volume | 12 | A 128² cube map whose colour gives the face and the texel. Explicit 3D coordinates on `XYZ` and `XYZRHW` vertices, through a `COUNT3` rotation, `CAMERASPACEREFLECTIONVECTOR` with local viewer on and off, through `COUNT3`, under perspective, `CAMERASPACENORMAL`, `CAMERASPACEPOSITION`. A 32×32×8 volume texture with explicit and generated coordinates |
| `mx_*` mixed devices | 7 | Always on a mixed device. Lit quadrants drawn alternately with hardware and software processing, from user pointers and from `D3DUSAGE_SOFTWAREPROCESSING` vertex buffers. A scene whose quadrants each switch mode and use vertex fog, texgen, two-weight blending or points. Indexed blending with indices up to 255 in software mode, then a hardware draw |

Three details of the case design keep the comparison exact:

- No sample sits on a cube edge, where two components of the direction are
  equal. The dome normals are deliberately not symmetric.
- The volume `w` is affine in x and y, so it interpolates exactly and every
  sample is at least a tenth of a slice from a boundary. The first version
  hit boundaries at the triangles' shared diagonals.
- Indexed blending uses indices up to 8 only. That is the runtime's palette
  with hardware vertex processing, explained below. Indices up to 255 run
  in software mode in `mx_indexed_high`.

`docs/evidence/ffp2/src/run-repro.sh <lab-dir> <vm> <out-dir> [--prog
d3d9_ffp2|d3d9_ffp] [--runs ...]` runs the probe in the autologon user's
session through `tests/guest/Run-Probe.ps1`. The default runs are x64 and
x86 on the hardware device, x64 with software processing, and x64 on a
mixed device in each mode, then the same set on the reference device. The
script writes one comparison per hardware run. `build.sh` builds both
probes in the toolchain image, and `make-table.py` builds `table.md`.

## How these features reach the UMD

The runtime turns fixed-function state into a vs_2_0 and a ps_2_0 shader
([`ffp.md`](ffp.md)). The new areas follow the same path:

- **Vertex blending.** The runtime's vertex shader does the blending. For
  indexed blending it reads `BLENDINDICES` and computes
  `a0 = index * c1.y + c1.w`. It then reads the matrix rows from
  `c[a0]` to `c[a0+3]`. A development build of the UMD dumped the shader
  and the `SetVertexShaderConst` calls. They showed `c1 = (0.5, 8, e, 175)`:
  matrix k sits at c175 + 8k. The runtime uploaded c175 to c242, which is
  matrices 0 to 8. A probe with every vertex on one index rendered
  correctly for indices 0 to 8 and wrongly from 12 up. The runtime did this
  whether the cap said 0 or 255. So the cap only tells applications what
  works. The UMD's relative constant addressing (Nine's `ARR` and the full
  256-constant upload) was not at fault.
- **Bump mapping.** The runtime's pixel shader applies the bump matrix
  itself. It needs the bump texture to sample as (U, V, L, 1), which
  `R8G8B8A8_SNORM` with alpha swizzled to 1 gives.
- **Cube and volume coordinates.** The runtime's vertex shader generates and
  transforms them. The UMD only samples.
- **Mixed devices.** Software-mode draws reach the UMD as the runtime's
  processed vertices, exactly as on a software device (`patches/mesa/0040`),
  and hardware-mode draws as on a hardware device. Nothing in the UMD
  depends on the device type.

## Bump-map formats: `patches/mesa/0043`

Vulkan has no format with signed U and V and unsigned luminance, so the UMD
had left out `L6V5U5` (61) and `X8L8V8U8` (62). The fix:

- adds both to the format table, for 2D, cube and volume textures, with the
  same operations as `V8U8` but no render-target use;
- maps both to `PIPE_FORMAT_R8G8B8A8_SNORM`, with the view's alpha
  swizzled to 1;
- converts the texels in `D3D9TextureSubdata`. Every CPU-to-GPU texture
  upload goes through that function: managed textures, locks backed by CPU
  memory, and copies from system-memory surfaces. U and V keep their signed
  value, scaled from 5 or 8 bits to 8 bits, and L goes into the positive
  half of blue.

Before: `bm_x8l8v8u8`, `bm_lum_x8l8v8u8`, `bm_lum_l6v5u5`, `bm_lum_rhw` and
`bm_stage1_lum` failed with `D3DERR_NOTAVAILABLE` on every GPU, in every
mode. After: they match the reference.

A lock that maps such a texture directly on the GPU still sees the
converted layout. That happens only for dynamic textures in
`D3DPOOL_DEFAULT`. No probe case uses one.

## Matrix palette: `patches/mesa/0044`

`MaxVertexBlendMatrixIndex` was 0. The reference reports 255, and its own
vertex processing supports that. The runtime's shader on this UMD supports
indices 0 to 8, as shown above, so the cap is now 8. An application that
checks the cap can use hardware vertex processing for a palette of up to 9
matrices. For larger palettes it still switches to software or mixed
processing, which supports 256 matrices (`mx_indexed_high`). The rendering
of indices 0 to 8 was already right before the change.

## What remains

- `vb_indexed_color` cannot be compared with the reference's own hardware
  vertex processing, which loses the device. The driver matches the
  reference fed by the runtime's software vertex processing.
- Indexed vertex blending with indices above 8 needs software vertex
  processing. That is a limit of the runtime's shader, not of the UMD.
- Dynamic `D3DPOOL_DEFAULT` textures in `L6V5U5` or `X8L8V8U8` show the
  converted texel layout to `Lock`.
- Not tested: `PREMODULATE`, W-buffering, multisampling, wide lines, and
  `texbem`/`texbeml` in application ps_1_x shaders with the new formats.
- Host, `Nvidia2`: during these runs the NVIDIA driver logged Xid 69
  (`ErrorCode 0000013d`, `vkr-ring-37` at 01:50:44 and `vkr-ring-70` at
  03:04:13). The failing `virgl_render_server` processes (2238723 and
  3048572) wrote their `vkQueueSubmit`/`vkCreateGraphicsPipelines` CS errors
  into the QEMU log of another worker's lab (`win3d-compat-nvidia2`), not
  of these labs. At 01:50:44 the `ffp2` Windows 11 lab was running
  `d3d9_ffp --ref`, and the Windows 10 lab had no Venus driver yet. The
  driver also logged `can't alloc VA space for mapping` at 02:57 and 03:05.
  The two one-off failures above coincide with it.

## Upstream

`0043` and `0044` belong in `arehnman/virtio-win-mesa` with `0040` to `0042`.

## Evidence index (`docs/evidence/ffp2/`)

| Path | What it shows |
| --- | --- |
| `src/d3d9_ffp2.cpp`, `src/build.sh` | The probe, and the build of it and of `d3d9_ffp` |
| `src/run-repro.sh`, `src/make-table.py` | The guest runner and the case table |
| `table.md` | Every case, before and after, per lab and mode, and the totals of both probes |
| `before/<lab>/` | Runs with the package at `82d8e51`. On `Nvidia2`, a lab with that package installed. On `AMD1`, the after lab with the package's own `viogpu_d3d10.dll` and `viogpu_wgl.dll` from `82d8e51` put back (sha256 `03aaab62…` x64, `bd836c21…` x86), compared with that lab's after reference runs. In the Windows 10 lab the guest stopped during the last two `d3d9_ffp` reference runs, so those comparisons use the after lab's reference runs |
| `after/<lab>/` | Runs with the package at `c51dec0`, in the lab `tests/e2e/run.sh --keep` built, after its Done checks |
| `after/<lab>/e2e/` | That e2e run: `results.txt` (12 of 12), `host.txt`, `build.txt`, console captures |
| `after/<lab>/rerun/` | The one-off failures run again |
| `*/yttrium-errors-*.txt` | The UMD's warnings during each probe, without dwm's |
