# F6: 32-bit D3D9 address-space exhaustion in the upload path

Compatibility finding F6 ([compat.md](compat.md#f6-valley-d3d9-on-amd1-hangs),
[unid3d.md](unid3d.md#f6-valley-d3d9-on-amd1)): Unigine Valley with Direct3D 9,
a 32-bit large-address-aware program, used 3.75 of its 4 GiB address space
within 90 s, then hung or crashed. The same exhaustion ended Valley D3D9 and
Heaven D3D9's exit on NVIDIA. `ORDERED_CONTEXT_WORKER=0` in `yttrium.ini`
avoided it, at about a third of the frame rate.

This page gives the cause, the fix (`patches/mesa/0091` and `0092`) and the
evidence. The work was done on 2026-10-19 on helios: NVIDIA 610.57.04 on
`Nvidia1`, RADV (Mesa 26.2.2) on `AMD1`, QEMU 11.1.1, virglrenderer 1.3.0 and
vmlab 0.28.0-alpha. The evidence is in `docs/evidence/f6va/`.

## Result

| | Before | After `0091` and `0092` |
| --- | --- | --- |
| Valley D3D9 address space | 3.7 to 3.97 GiB, largest free block 3 to 16 MiB | At most 2.07 GiB in 10 minutes, largest free block at least 1.8 GiB (`AMD1`, `Nvidia1`) |
| 4 MiB maps in the process | 300 to 420 | 6 to 13 |
| Upload buffers created | about 500 MB/s | about 50 MB/s |
| Valley D3D9, 10 minutes | Hang or crash | Clean exit on WM_CLOSE (`AMD1`, `Nvidia1`) |
| Heaven D3D9 on `Nvidia1` | Crash at exit (code 1653) | Clean exit, code 0 |

## Cause

The UMD's ordered context worker is Gallium's threaded context: the D3D9
frontend queues its calls, and a worker thread records them into Venus
command batches. A queued draw reads its vertex buffer when the worker
records it, not when the application calls it. Patch series `0001` to
`0090` handled that with a snapshot: at the Unlock of every write lock of a
vertex buffer, the frontend copied the locked range into the stream uploader
(an upload buffer, which is a mapped blob), and draws until the next lock
read the copy.

A trace of Valley's D3D9 locks (`D3D10UMD_D3D9_TRACE=1`;
[lock-flags.txt](evidence/f6va/nvidia1/diag/lock-flags.txt)) shows what that
cost. Valley locks one 4 MiB vertex buffer whole, with
`D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE` and no range, about 100 times a
second: 4,052 of its 7,354 buffer locks in a 40 s run, 17 GB of locked
range. Each lock took a 4 MiB copy, larger than the uploader's 1 MiB page,
so each copy needed a new 4 MiB upload buffer. Copies of smaller locks were
placed at an upload offset at least as large as their offset in the vertex
buffer, which wasted most of an upload buffer too. With every upload buffer alive
until the last batch that used it retired, and the threaded context free to
queue many frames ahead, the process held 300 to 420 maps of 4 MiB
(`vasample.exe`, below).

Two earlier attempts in this work did not fix it. They are recorded because
they narrow the cause:

- Packing the snapshots tightly, and releasing a snapshot two presents after
  its lock, cut the wasted space but not the volume. Valley still hung at
  3.97 GiB on `Nvidia1`
  ([valley-d3d9-0091v1](evidence/f6va/nvidia1/valley-d3d9-0091v1/); three
  of our VMs shared the card at the time). Its upload statistics show 41 GB
  of upload buffers in the first 80 s of rendering
  ([upload-stats.txt](evidence/f6va/nvidia1/valley-d3d9-0091v1/upload-stats.txt)).
- Copying, at each draw, only the vertex range that the draw reads still
  created about 1 GB/s of upload buffers in a 2-minute diagnosis run (its
  log was not kept), because Valley's draws from that buffer read large
  ranges.

## Fix

### `0091`: no whole-range copies of vertex-buffer locks

- A buffer lock with both `DISCARD` and `NOOVERWRITE` is a `NOOVERWRITE`
  lock. DXVK's D3D9 does the same ("Ignore DISCARD if NOOVERWRITE is set",
  `d3d9_device.cpp`). Before, the frontend mapped it with both
  `PIPE_MAP_DISCARD_RANGE` and `PIPE_MAP_UNSYNCHRONIZED`.
- A `DISCARD` lock needs no copy. The threaded context gives it new memory,
  either a staging upload that it copies into the buffer in order or new
  storage that replaces the old in order.
- A `NOOVERWRITE` lock needs no copy. The application promises not to write
  data that queued draws read.
- After a plain write lock, which the threaded context orders but whose data
  queued draws may still read, each draw copies only the vertex range it
  reads into the stream uploader. This lasts until the buffer is locked
  again or two presents pass. The draw sees the data as it was when the
  draw was called, which is what D3D9 requires.
- These copies, and user-pointer vertex data (`DrawPrimitiveUP`), are packed
  at the uploader's next free offset. When that makes a stream's buffer
  offset negative, the draw is rebased: its first vertex becomes vertex 0,
  each per-vertex stream's buffer offset grows by `first * stride`, and
  `start_vertex` or `index_bias` drops by `first`. Every fetch address stays
  the same.
- At Present, if the winsys reports its upload buffers over budget (`0092`),
  the frontend flushes and waits for the worker. The threaded context then
  cannot queue many frames of uploads.

### `0092`: a budget for live upload buffers in 32-bit processes

The winsys counts the bytes of live upload buffers. After each context
flush, while they exceed the budget, it submits the pending batch group,
retires completed batches, and waits for the oldest batch. The budget is
256 MiB in a 32-bit process and unbounded in a 64-bit one.
`UPLOAD_BUDGET_MB` in `C:\ProgramData\Yttrium\yttrium.ini` overrides it, and
0 turns it off. With the `0091` changes Valley stays far below the budget
(peak 36 MiB of live upload buffers), so the budget is a bound for other
programs, not what fixes Valley.

`UPLOAD_STATS=1` in `yttrium.ini` logs to `yttrium-errors.log`, every 5 s,
the created and live counts and sizes of upload buffers, other buffers,
textures, buffer-replacement storages, the winsys's Venus memory mappings,
blob-mapped and heap-backed resources, and every blob map escape. It is off
by default.

## Evidence

`docs/evidence/f6va/src/vasample.cpp` (built by `build.sh`) samples a
process's address space with `VirtualQueryEx` and `GetProcessMemoryInfo`:
used and free address space below 4 GiB, the largest free block, the
4 MiB regions grouped by page protection, the commit charge and the working
set. `sample-va.sh` runs it in the guest every 10 s for the life of a
program. The 4 MiB regions are blob maps. `ReadProcessMemory` cannot read
them, and they have the uncached or write-combined protection the KMD maps
blobs with.

### Valley D3D9, 10 minutes

`tests/apps/compat/run.sh --only valley-d3d9 --seconds 600` in the worker's
own labs (`COMPAT_LAB`, `COMPAT_EV`), with the package built at `d5fa281`
(`0001` to `0092`):

| GPU | Address space, peak | Largest free block, lowest | Exit | Frame rate (OCR) | Evidence |
| --- | --- | --- | --- | --- | --- |
| `Nvidia1` | 2.07 GiB | 1.79 GiB | Clean | 7.9 | [nvidia1/valley-d3d9](evidence/f6va/nvidia1/valley-d3d9/) |
| `AMD1` | 2.03 GiB | 1.84 GiB | Clean | 9.8 | [amd1/valley-d3d9](evidence/f6va/amd1/valley-d3d9/) |

`va.txt` in each directory has the samples. The frames render the scene
correctly (`console-*.png`).

The upload statistics of a 2-minute run on `AMD1` with the same package
([upload-stats-120s.txt](evidence/f6va/amd1/upload-stats-120s.txt)) show
7.8 GB of upload buffers created in 150 s, at most 36 MiB of them alive, and
at most 413 MiB of blob maps. With the first attempt, Valley created 41 GB
in about 80 s
([upload-stats.txt](evidence/f6va/nvidia1/valley-d3d9-0091v1/upload-stats.txt)).

### Frame rate

The frame rate is read with OCR from Valley's on-screen counter, so it is
noisy, and it changes with the scene. On `Nvidia1`, over the same first
two minutes of the benchmark:

| Run | Frame rate (OCR) |
| --- | --- |
| Worker off (`ORDERED_CONTEXT_WORKER=0`) | 7.1 |
| Worker on, `0091` and `0092` | 13.0 |

On `AMD1`, a 2-minute run with `0091` and `0092` averaged 10.0 fps and the
10-minute run 9.8 fps. With the worker off, unid3d measured 6.1 fps there
([unid3d.md](unid3d.md)). Before the fix, the sweep saw 19 fps in the first
seconds on `AMD1`, falling to 1 fps as the address space filled. The
10-minute mean on `Nvidia1` is 7.9 fps, because the scenes later in the
benchmark are heavier. Another worker's VM shared `AMD1` during its runs.
Over the same span, the worker-on rate is about 1.6 to 1.8 times the
worker-off rate. It stays below the 19 fps of the first seconds before the
fix, but that figure was measured on the lightest scene.

### Heaven D3D9

`tests/apps/compat/run.sh --only heaven-d3d9` (150 s, then WM_CLOSE), same
package. Heaven is 32-bit and not large-address-aware.

| GPU | Exit | Frame rate (OCR) | Evidence |
| --- | --- | --- | --- |
| `Nvidia1` | Clean, code 0 | 15.8 | [nvidia1/heaven-d3d9](evidence/f6va/nvidia1/heaven-d3d9/) |
| `AMD1` | Clean, code 0 | 17.4 | [amd1/heaven-d3d9](evidence/f6va/amd1/heaven-d3d9/) |

Before, on `Nvidia1`, Heaven D3D9 rendered at 10.5 fps, then exited with
code 1653 after a crash in `textinputframework.dll`, and the render server
logged a `vkCmdPipelineBarrier` CS error ([unid3d.md](unid3d.md)).

### Diagnosis runs

[nvidia1/diag/](evidence/f6va/nvidia1/diag/) holds the address-space
samples that located the cause, all on `Nvidia1`:

- `va-worker-off.txt`: worker off, 1.65 GiB at 120 s, seven 4 MiB regions.
- `va-worker-on-0091.txt`: worker on, with the packed snapshots of the first
  attempt, 3.6 GiB, 262 regions of 4 MiB.
- `lock-flags.txt`: the lock trace above.

### End-to-end and probes

`tests/e2e/run.sh --template windows-11` in the worker's own labs (`--lab`),
with the package of `d5fa281` (`patches/mesa` `0001` to `0092`; the e2e
script rebuilt it because `build/mesa/README.md` had uncommitted
documentation changes):

| GPU | Result | Evidence |
| --- | --- | --- |
| `Nvidia1` | 12 of 12 | [e2e/windows-11-nvidia1](evidence/f6va/e2e/windows-11-nvidia1/) |
| `AMD1` | 12 of 12 | [e2e/windows-11](evidence/f6va/e2e/windows-11/) |

The e2e run includes the x86 (32-bit) D3D9, D3D11, D3D12, OpenGL and
Vulkan probes, each rendering on the GPU in the console capture. No NVIDIA
Xid was logged for these labs.

### Other approaches considered

- Recycling upload buffers when their fence passes: the winsys already does
  it. A destroyed upload buffer goes back to a pool of up to 64 MiB, and the
  rest are unmapped when the last batch that used them retires. Valley's
  problem was the number of buffers in flight, not buffers left behind
  after their fence. The escape-level count of blob maps matched the address
  space, and no unmap failed.
- Mapping windows of blob memory (`MapViewOfFile` style): each upload
  buffer is its own blob, which the KMD maps into the process with
  `DxgkCbMapMemory`. Mapping windows would need a new KMD interface.
  Copying less made it unnecessary.

## What remains

- The budget in `0092` waits on the GPU. A 32-bit program that keeps more
  than 256 MiB of upload buffers in flight runs slower instead of failing.
  No tested program reached it.
- Valley's frame rate is measured with OCR. A frame-time measurement would
  be more precise.
- F7, the threaded-context use-after-free behind Heaven D3D11's heap
  corruption, is a separate finding ([unid3d.md](unid3d.md#f7-heaven-d3d11-on-amd1)).
