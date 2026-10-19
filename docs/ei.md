# ExecuteIndirect with state-changing command signatures

`D3D12ExecuteIndirect`, one of Microsoft's DirectX-Graphics-Samples, drew
only its clear colour on AMD1 and Nvidia1 ([d3d12app.md](d3d12app.md)).
`patches/vkd3d-proton/0006` fixes this. Without `VK_EXT_device_generated_commands`,
vkd3d-proton now emulates `ExecuteIndirect` for command signatures that
change root constants or root descriptors. The fix changes nothing on the
host and nothing in Venus. The work was done on 2026-10-19 on helios: RADV
(Mesa 26.2.2) on `AMD1`, NVIDIA 610.57.04 on `Nvidia1` and `Nvidia2`,
virglrenderer 1.3.0, the package built from `d6f52e4`. The evidence is in
`docs/evidence/ei/`.

## Result

| Check | AMD1 | Nvidia1 | Nvidia2 |
| --- | --- | --- | --- |
| Repro `ei_repro`: the GPU against WARP, and both against the expected values | pass, 0 of 16384 pixels differ | pass, 0 of 16384 | pass, 0 of 16384 |
| `D3D12ExecuteIndirect`, unmodified, against the same sample on WARP | pass, coverage ratio 1.13 | pass, coverage ratio 1.12 | not run |
| The five samples (`tests/apps/d3d12/run.sh`): exit 0, no app-local module | 5 of 5 | 5 of 5 | not run |
| `tests/e2e/run.sh --template windows-11` | 12 of 12 | 12 of 12 | not run |

The Nvidia2 repro ran before Nvidia2 was reserved for another worker. The
sample, e2e and second repro runs then moved to Nvidia1.

## Cause

`D3D12ExecuteIndirect` builds a command signature with a root
constant-buffer view argument followed by a draw. vkd3d-proton implements
any signature that changes state with `VK_EXT_device_generated_commands`
(DGC). RADV and the NVIDIA driver have DGC on the host, but Venus does not
forward it. Without DGC, `d3d12_command_signature_create` dropped the state
arguments, and `ExecuteIndirect` returned without drawing
([d3d12app.md](d3d12app.md#executeindirect-draws-nothing) has the trace).

## Choosing the fix

The brief offered two fixes.

**A: DGC through Venus.** Venus encodes every Vulkan command in a protocol
that `venus-protocol` generates from `vk.xml`. Its current tree
(`gitlab.freedesktop.org/virgl/venus-protocol`, commit ca19b63 of 2026-09-06)
lists no device-generated-commands extension. Venus in newer Mesa and
virglrenderer therefore has no support to backport. This fix would need:

- a protocol extension, with two new object types: indirect commands layouts
  and indirect execution sets;
- the guest driver's object tracking for those types;
- the host renderer's dispatch for them.

It would also work only with our own `virgl_render_server`, never with a
stock host.

**B: emulation in vkd3d-proton.** This needs one component, which we
already patch, and it works on any host. B was chosen.

## The fix

Patch `0006` adds an emulation path, used only when the device has no DGC.
The path has three parts.

### Root signatures

Every root signature with root parameters puts them in vkd3d-proton's push
UBO block (`VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK`), instead
of push constants. Root CBVs become raw VAs, as root SRVs and UAVs already
are. vkd3d-proton already takes this path when root parameters exceed
`maxPushConstantsSize`. The shader reads root constants and root descriptor
addresses from memory, so the GPU can write them per draw. The root
signature is created before anything shows whether it will be used with
`ExecuteIndirect`, so the choice applies to every root signature.

### Command signatures

A signature that changes root constants or root CBV, SRV or UAV addresses,
and ends in a draw, indexed draw, mesh draw or dispatch, gets a patch list.
For each argument word, the list names its source offset in the
application's argument record and its destination in a per-sequence slot:

- **Root constants:** the constant's word in the root parameter block.
- **Root descriptors:** the two words of the raw VA.
- **Incrementing constants:** no source word. The CPU writes the sequence
  index into each slot.
- **The action's arguments:** placed after the root parameter block, 16-byte
  aligned.

A slot is a multiple of 256 bytes, so every slot is a valid UBO offset.

### ExecuteIndirect

When a command list records `ExecuteIndirect` with such a signature, it:

1. allocates `MaxCommandCount` slots in upload memory;
2. records vkd3d-proton's existing patch shader
   (`cs_execute_indirect_patch.comp`), which copies the argument buffer into
   the slots and stops at the count buffer's value. This runs in the command
   list's own order, outside the render pass, followed by a barrier to
   indirect and uniform reads. Under fallback predication, the predicate is
   folded into the count;
3. flushes the bound state as for a normal draw or dispatch;
4. fills every slot on the CPU with the currently bound root parameters and
   zeroed action arguments. The GPU patch then overwrites the words the
   signature sets. Sequences past the count keep zero arguments and draw or
   dispatch nothing;
5. for each sequence, pushes the slot as the root UBO and records one
   `vkCmdDrawIndirect`, `vkCmdDrawIndexedIndirect`,
   `vkCmdDrawMeshTasksIndirectEXT` or `vkCmdDispatchIndirect` reading the
   slot;
6. clears the state that the signature set, as D3D12 requires, and marks all
   state dirty.

vkd3d-proton logs one line per emulated signature:

```
info:d3d12_command_signature_init_state_template_emulated: Emulating ExecuteIndirect state (10 patch words, slot stride 256) without EXT_dgc.
```

## Evidence

All under `docs/evidence/ei/`:

| Path | What |
| --- | --- |
| `src/ei_repro.cpp`, `src/build.sh` | The repro and its build in the shared toolchain image |
| `src/run-repro.sh` | Runs the repro in a lab: GPU, WARP, compare |
| `src/sample-warp.sh`, `src/compare-sample.py` | Runs the unmodified sample on WARP and compares its console capture with the GPU run's |
| `amd1-repro/`, `nvidia1-repro/`, `nvidia2-repro/` | `repro.txt` (the three outputs), `ei_gpu.png`, `ei_warp.png`, and the vkd3d-proton log with the emulation lines |
| `samples-amd1/`, `samples-nvidia1/` | The five samples (`samples.json`, captures, run and module lists, DXVK logs, `app-errors.txt`), and the WARP run of `D3D12ExecuteIndirect`: `console-D3D12ExecuteIndirect-warp.png`, `sample-compare.txt` |
| `e2e/amd1/`, `e2e/nvidia1/` | `tests/e2e/run.sh` evidence; `results.txt` ends `PASS: 12 of 12`, and `build.txt` shows the package reused from `d6f52e4` |

### The repro

`ei_repro` records two `ExecuteIndirect` calls in one command list:

- **Graphics:** a signature of root constants (colour), a root CBV (cell
  offset) and a draw. It records 16 sequences on a 4×4 grid, with a count
  buffer of 13. A root constant outside the signature (the quad size) is set
  on the CPU, and the signature's own parameters are first set to wrong
  values that `ExecuteIndirect` must replace.
- **Compute:** a signature of a root constant and a dispatch, with 8
  sequences of 4 groups writing to a root UAV.

`ei_repro gpu` runs on the default adapter through the package's
`d3d12.dll`. `ei_repro warp` runs in a second process, from a directory
holding Microsoft's `d3d12.dll` (the package keeps it as
`System32\vmlab-win3d\d3d12-native.dll`) and `System32\D3D12Core.dll`.
Outside System32, the stock `d3d12.dll` looks for its core next to itself,
and fails with `0x887E0003` otherwise. `ei_repro compare` checks the 16 cell
centres and 64 UAV words of each run against the expected values, and then
the two images against each other. Each check allows 1/255 per channel: on
NVIDIA, colour 0.5 converts to 127, while WARP and RADV give 128.

### The sample on WARP

The sample animates 1024 triangles that start at `rand()` positions, and
its compute pass culls those outside the middle of the window. Two captures
are never of the same frame, so `compare-sample.py` compares what they must
share:

- the client area, found by the clear colour;
- the share of that area the triangles cover (within a factor of 1.5);
- how that coverage spreads across eight vertical bands (each band within
  0.1).

The samples export `D3D12SDKVersion = 618` and `D3D12SDKPath = .\D3D12\`.
Microsoft's `d3d12.dll` running from outside System32 needs the matching
Agility SDK core there, so `sample-warp.sh` deploys `D3D12Core.dll` from the
pinned `Microsoft.Direct3D.D3D12` 1.618.3 package that
`build/d3d12-samples/` downloads. Without it, every sample on WARP fails with
`0xC0000409`. The before capture
(`docs/evidence/d3d12app/amd1/console-D3D12ExecuteIndirect.png`) scores a
coverage of 0.000. After the fix, the GPU covers 0.202 on AMD1 and 0.206 on
Nvidia1, and WARP covers 0.178 and 0.183.

### Other observations

- In the AMD1 sample lab, `dwm.exe` crashed once in `viogpu_d3d10.dll`
  (`0xc000041d`) during `D3D12HelloTexture`, in the first boot after the
  install (`samples-amd1/app-errors.txt`). Every sample still exited 0 and
  rendered. This looks like the first-boot dwm crash in
  [d3d12app.md](d3d12app.md#nvidia-dwm-crashes-in-the-first-boot-after-the-install),
  which until now had been seen only on NVIDIA. This patch does not touch it.
- Labs from the `windows-11` template report "Windows License is expired"
  and shut down about an hour after boot, even with a NAT NIC.

## What remains

- **Vertex and index buffer view arguments** are bound on the CPU and cannot
  come from the GPU in this scheme, so signatures with them are still
  dropped, as before. Emulating them needs a pass that finds the VkBuffer
  for each GPU VA. Alternatively, the shader could fetch vertices itself.
- **Root CBVs through push descriptors.** On NVIDIA, vkd3d-proton normally
  binds root CBVs as push descriptors, which it measured as faster. Without
  DGC, they are now raw VAs on every vendor.
- **The push UBO for every root signature** costs a 256-byte upload and a
  push-descriptor update per draw that changes root state, instead of a push
  constant update. The four other samples, the D3D12 probe and the e2e runs
  pass with it. No frame rate was measured.
- **Cost per sequence.** Each sequence records one push-descriptor update
  and one indirect command, so a signature with `MaxCommandCount` in the
  tens of thousands records that many commands. The sample uses 1024.
- **Ray tracing** signatures (`DispatchRays` with state) stay unimplemented,
  as with DGC.
