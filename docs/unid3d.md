# Unigine D3D9 and D3D11 findings F5, F6 and F7

The compatibility sweep ([compat.md](compat.md)) left three Unigine
findings in the Direct3D 9 and 11 path:

- **F5:** Heaven and Valley with D3D9 on NVIDIA skipped every draw that uses
  a depth bias.
- **F6:** Valley with D3D9 on AMD1 froze mid-run and did not exit on
  WM_CLOSE.
- **F7:** Heaven with D3D11 on AMD1 crashed once with heap corruption.

This page gives the cause, the fix and the evidence for each one. The work
was done on 2026-10-19 on helios: NVIDIA 610.57.04 on `Nvidia1` and
`Nvidia2`, RADV (Mesa 26.2.2) on `AMD1`, QEMU 11.1.1, virglrenderer 1.3.0
and vmlab 0.28.0-alpha. The evidence is in `docs/evidence/unid3d/`.

## Result

| Finding | Cause | Change | Status |
| --- | --- | --- | --- |
| F5 | The UMD passed the D3D9 depth bias only in Vulkan's float representation, which NVIDIA does not offer. NVIDIA also doubles an inexact integer bias on D16 and D24 | `patches/mesa/0090` | Fixed |
| F6 | F6_CAUSE | F6_CHANGE | F6_STATUS |
| F7 | F7_CAUSE | none | F7_STATUS |

## F5: D3D9 depth bias on NVIDIA

### Cause

D3D9's `D3DRS_DEPTHBIAS` is a float offset in the depth buffer's own units.
D3D10 and 11 give an integer count of the format's minimum resolvable
difference, r, which is what Vulkan's `depthBiasConstantFactor` normally
means. Patch `0006` passes the D3D9 value through
`VK_EXT_depth_bias_control` with
`VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT`, and it failed the pipeline when the
device lacks that representation. NVIDIA's driver reports
`depthBiasControl`, `leastRepresentableValueForceUnormRepresentation` and
`depthBiasExact`, but not `floatRepresentation` (host `vulkaninfo`). RADV
reports all four. So on NVIDIA every pipeline with a D3D9 depth bias failed,
and the draws using it were skipped. That is 69,686 skipped draws in
Heaven's sweep run
(`docs/evidence/compat/nvidia2/heaven-d3d9/yttrium-log.txt`). Heaven drew
dark frames at about 1 fps, and Valley crashed.

### Fix

`patches/mesa/0090` keeps the float path where the device has it. Without
it, the patch converts the bias to Vulkan's integer units by dividing by r,
as DXVK's D3D9 does:

| Depth format | r used |
| --- | --- |
| D16 | 2^-16 |
| D24 (`X8_D24`, `D24_S8`) | 2^-24 |
| D32F, with the force-UNORM representation | 2^-24 |
| D32F, without force-UNORM | 2^-23 (exact for depths in [0.5, 1)) |

The patch also requests `depthBiasExact` for these pipelines when the device
offers it. Without it, NVIDIA applied twice the requested bias on D16 and
D24. The Vulkan spec allows this "to ensure a minimum resolvable distance".
The patch now enables the force-UNORM representation only when the device
reports it. Before, it was also requested for D24 on devices that did not
report it.

### Evidence

`docs/evidence/unid3d/src/d3d9_depthbias.cpp` (built by `build.sh`, run by
`run-depthbias.sh`) draws a red quad at depth 0.5, then a green one 0.004
behind or in front of it with a depth bias, on every depth format the device
offers. It checks which quad wins. It then bisects for the smallest bias that
wins and logs the device's bias scale, which is 1.0 on Microsoft's reference
rasterizer.

| Run | Cases | D16 scale | D24X8 scale | D24S8 scale | D32F scale |
| --- | --- | --- | --- | --- | --- |
| Reference rasterizer (x64) | 30 of 30 | 1.004 | 1.000 | 1.000 | 1.000 |
| `Nvidia2`, `0090` without `depthBiasExact` | 17 of 20 | 2.000 | 2.000 | 2.000 | 1.000 |
| `Nvidia2`, `0090` (x64 and x86) | 20 of 20 each | 1.000 | 1.000 | 1.000 | 1.000 |
| `AMD1`, `0090` (float path, x64 and x86) | AMD1_DB | | | | |

Logs: `docs/evidence/unid3d/depthbias/`. The first `0090` build had no
`depthBiasExact`. The x86 probe matches the x64 one. Unigine is 32-bit.

Unigine with `0090`:

- Heaven D3D9 on `Nvidia2` rendered the scene correctly
  (`nvidia2-0090-without-exact-busy-card/heaven-d3d9/console-2.png`; before
  the fix the same frame was dark, `docs/evidence/compat/nvidia2/heaven-d3d9/console-2.png`)
  and exited cleanly. The UMD logged no depth-bias line. This run used the
  first `0090` build.
- The card had four VMs on it at the time, and the run hit BAR1 exhaustion
  (worker `nvva`). Uniform-buffer creation failed
  (`venus_uniform_buffer_create_failed`), and Valley crashed after a
  `vkCmdPipelineBarrier` CS error. Those failures are nvva's, not depth bias.
  The rerun on a quiet card is below.

NV1_RERUN

## F6: Valley D3D9 on AMD1

F6_BODY

## F7: Heaven D3D11 on AMD1

F7_BODY

## Proof runs

PROOF

## What remains

REMAINS
