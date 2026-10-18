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
| F6 | Valley, a 32-bit process, runs out of address space (3.75 of 4 GiB in 90 s). The UMD's ordered-context-worker upload path creates and maps about 340 MB/s of 4 MiB upload buffers and keeps hundreds of them alive | none shipped; an experiment is in `docs/evidence/unid3d/src/` | Cause found, not fixed; workaround `ORDERED_CONTEXT_WORKER=0` |
| F7 | A use-after-free in the UMD: the threaded context's worker runs a queued flush-label callback whose payload is already freed | none | Reproduced under PageHeap (2 of 5 runs); cause located, not fixed |

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

The quiet-card rerun on `Nvidia1` (two of our VMs on the card;
`docs/evidence/unid3d/nvidia1/`) used the final `0090` build plus a
diagnostic log line that is off by default:

- **Heaven D3D9** rendered every frame correctly at 10.5 fps (before: 1.1
  fps and dark frames). It closed its window on WM_CLOSE, but then exited
  with code 1653 after a crash in `textinputframework.dll`, and the render
  server logged a `vkCmdPipelineBarrier` CS error.
- **Valley D3D9** hung like F6 on AMD1, with the same CS error.

The UMD logged no depth-bias line in either run. The remaining failures
belong to F6: the same 32-bit address-space exhaustion. Heaven is not
large-address-aware, so it has only 2 GiB.

## F6: Valley D3D9 on AMD1

### What happens

The fresh repro on AMD1 matched the sweep exactly
(`docs/evidence/unid3d/amd1-run2/valley-d3d9/`). The frame froze, and the
render server logged `vkCmdBeginRenderPass resulted in CS error`. Valley did
not exit on WM_CLOSE. Another run crashed with `0xC0000005` at
`Unigine_x86.dll+0x3cc176`, the offset of the sweep's Nvidia2 Valley crash
(`amd1/valley-d3d9/`). The frame rate fell from 19 to 1 fps before the
crash.

### Cause

`docs/evidence/unid3d/src/sample-mem.sh` samples the guest process and the
lab's render server every few seconds (`amd1-mem/`):

| Valley | Virtual size at 25 s | at 60 s | at 90 s | Result |
| --- | --- | --- | --- | --- |
| D3D9 | 1.3 GiB | 3.3 GiB | 3.7 GiB (of 4 GiB, large-address-aware) | Hang |
| D3D11 | 0.4 GiB | 1.3 GiB | 1.7 GiB (3.1 GiB at 175 s) | Clean |

`docs/evidence/unid3d/src/vaquery.cpp` walks the process's address space
(`amd1-va/vaquery-45.txt`). Only 121 MiB were free, and the largest free
block was 2.6 MiB. 430 committed regions of exactly 4 MiB held 1.7 GiB.

A diagnostic build logs every resource the UMD creates and destroys
(`src/diag-resource-log.patch`, enabled with `RESOURCE_DIAG=1` in
`C:\ProgramData\Yttrium\yttrium.ini`). In 75 s Valley created 6,587
buffers with `bind=VERTEX|INDEX|CONSTANT` and `usage=STREAM`. These are the
stream uploader's upload buffers, 25.8 GB in total, and each one is a
mapped blob. Hundreds were alive at once.

Switching off parts of the UMD in `yttrium.ini` narrows it down
(`amd1-noreplace/`, `amd1-noworker/`):

| Setting | Valley D3D9 virtual size | Result |
| --- | --- | --- |
| default | 3.8 GiB | Hang |
| `BUFFER_REPLACEMENT=0` | 3.8 GiB | Hang |
| `ORDERED_CONTEXT_WORKER=0` | 1.7 GiB | Clean, 6.1 fps, renders correctly |

So the ordered-context-worker path is the source. With it, the D3D9
frontend captures every vertex-buffer write lock into the stream uploader,
and the threaded context stages `DISCARD` maps there. Each capture keeps its
upload page referenced until the buffer is locked again. The render
server's CS error has no "failed to look up object" line, so it rejected
the encoding of the command itself. The most likely cause is an allocation
failing at the address-space limit and leaving a malformed command in the
stream. The exact failing allocation was not identified.

### What was tried

`src/experiment-d3d9-snapshot-only-nooverwrite.patch` captures only
`D3DLOCK_NOOVERWRITE` writes. With it, Valley D3D9 exited cleanly on AMD1,
and its peak virtual size fell to 2.9 GiB (`amd1-0091/`). It still ran at
2 fps, and the diagnostic log showed 48,698 upload buffers (193 GB) created
in about 3 minutes. The churn comes from more than the captures. The patch
is not in the series, because it does not fix the finding and changes the
locking model without a full regression run.

### Workaround and next step

`ORDERED_CONTEXT_WORKER=0` in `C:\ProgramData\Yttrium\yttrium.ini` makes
Valley D3D9 run cleanly, but it applies to every program, and only Valley
was tested with it. A fix needs the upload volume per lock found and cut,
for example by uploading only the written range. It also needs the
in-flight upload memory bounded for 32-bit processes.

## F7: Heaven D3D11 on AMD1

Heaven D3D11 ran five times on AMD1 with full PageHeap and WER local
dumps enabled for `Heaven.exe` (`src/pageheap.sh`; the IFEO values that
`gflags /p /enable /full` writes). The runs are in
`docs/evidence/unid3d/f7-pageheap/try1` to `try5`:

| Try | Result |
| --- | --- |
| 1 | Crash at about 65 s, `0xC0000005` in `viogpu_d3d10.dll+0xe8c6b` |
| 2, 3, 5 | Clean (7.5 fps under PageHeap) |
| 4 | Crash at about 20 s, the same address |

Both crashes are the same use-after-free (`f7-pageheap/dump-analysis.txt`,
read with `src/minidump-summary.py` and the unstripped x86 DLL of the same
build). The faulting instruction is the first load in
`yttrium_threaded_set_flush_label` (`yttrium_context.c`). It is called from
`tc_batch_execute` on the threaded context's worker thread. Its argument,
`esi = 0x4e8f8f90`, is a 0x70-byte PageHeap block at the end of a freed
page. That size is a `struct yttrium_threaded_flush_label` (108 bytes).
That struct is freed only by the callback itself. So the worker ran a
callback call whose payload had already been freed: a callback executed
twice, or a stale call replayed from a reused batch. This tree's
`u_threaded_context` is heavily modified (variable batch sizes,
`batch_capacity_slots`), which makes a batch-bounds error the first suspect.
Without PageHeap, a write through the stale payload would land in a reused
heap block. That would explain the sweep's one-off `0xC0000374`, but it is
not proven.

The process's virtual size stayed below 1.8 GiB. Heaven is 32-bit and not
large-address-aware, so its limit is 2 GiB. F7 is not the F6 address-space
problem.

## Proof runs

The e2e runs, the depth-bias probe and the F7 runs used the package built
at `98444b5` (`patches/mesa` up to `0044` plus `0090`). The `Nvidia1`
Unigine run used the same series plus the diagnostic log line, which is
off by default:

| Run | Result | Evidence |
| --- | --- | --- |
| `tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1` | 12 of 12 | `docs/evidence/unid3d/e2e/windows-11-nvidia1/` |
| `tests/e2e/run.sh --template windows-11 --host-gpu AMD1` | 12 of 12 | `docs/evidence/unid3d/e2e/windows-11/` |
| `d3d9_depthbias`, `Nvidia2`, x64 and x86 | 20 of 20 each, scale 1.0 | `depthbias/nvidia2-0090/` |
| Unigine D3D9 through `tests/apps/compat/run.sh`, `Nvidia1` | Heaven renders correctly; both runs end in F6 failures | `nvidia1/` |

No run logged an NVIDIA Xid for our labs. The compat runs record the
kernel's GPU lines in `host-kernel.txt`, and there are none. The depth-bias
probe was not run on AMD1. RADV takes the float path, which `0090` changes
only by asking for an exact bias, and the AMD1 Unigine D3D9 runs with
`0090` rendered normally until F6 struck.

`tests/apps/compat/run.sh` gained `COMPAT_LAB` and `COMPAT_EV`, so that a
worker can run it in its own lab and write elsewhere. This is the same
change as worker gzdoom's.

## What remains

- F6: the address-space exhaustion of 32-bit D3D9 programs through the
  ordered-context-worker upload path, and the upload churn behind Valley's
  2 fps. The same mechanism ends Heaven D3D9 and Valley D3D9 on NVIDIA.
- F7: the use-after-free of the queued flush label in the threaded
  context. The next step is to check `tc_batch_execute`'s bounds against
  the variable `batch_size_slots`/`batch_capacity_slots`, and to log each
  callback's payload pointer when it is queued and when it runs.
- The depth-bias probe on AMD1, and D32F without the force-UNORM
  representation (no host here lacks it).
