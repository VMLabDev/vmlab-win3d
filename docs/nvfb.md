# nvfb: Venus feedback on the RTX 4060 Laptop

On Zeus's RTX 4060 Laptop GPU, every client that drives Venus through the
Vulkan API hung on its first fence: the Vulkan probes, D3D12 through
vkd3d-proton, and OpenGL through Zink. The acceptance run passed 7 of 12
([`zeus.md`](zeus.md#nvidia-rtx-4060)). This page gives the cause, the
two guest fixes `patches/mesa/0065` and `0066`, the evidence, and what
remains. Evidence is in `docs/evidence/nvfb/`. Investigated on 2026-10-19.

## Result

| Item | Result |
| --- | --- |
| Cause | In the guest, the host GPU's writes to NVIDIA memory type 2 (`HOST_VISIBLE` and `HOST_COHERENT`, uncached system memory) are not visible to the guest CPU. The guest reads a stale line from the CPU cache, and the GPU's write never invalidates that line. Venus put its feedback buffer (fence, semaphore, event and query status) in that type. The host itself sees the same writes |
| Owner | The guest Venus driver chose the memory type. The underlying behaviour belongs to the host platform, an Intel CPU host with this NVIDIA GPU. On helios (an AMD CPU) a guest on an RTX 5090 sees every GPU write in type 2 (`helios/nv1-fbguest-0b26b36/`), so 0067 changes nothing there |
| Fix | `patches/mesa/0065`: Venus feedback buffers use a `HOST_COHERENT` and `HOST_CACHED` type (NVIDIA type 3), where the GPU's writes reach the guest. `patches/mesa/0066`: at device creation Venus checks that one fence feedback write lands, and disables feedback for that device if it does not. `patches/mesa/0067`: the same check tests the uncached type, and where the GPU's writes do not arrive, Venus backs that type with the cached one for every application (this also fixes the black OpenGL readback) |
| Zeus Nvidia1, `tests/e2e/run.sh --template windows-11`, package defaults | With 0065 and 0066 (`6dabb22`): runs 1 and 3 **12 of 12**, run 2 **11 of 12**, a black OpenGL readback. With 0067 as well (`0b26b36`, `main` merged): ZEUSFINAL |
| Regressions | See [Proof](#proof) |

## The symptom

`vn_get_fence_status` (`src/virtio/vulkan/vn_queue.c`) returned
`VK_ERROR_UNKNOWN` with "ERROR: ffb must be signaled now": the renderer said
the fence had signalled, but the fence's feedback slot still read
`VK_NOT_READY`. With the Venus `VN_PERF` options
`no_fence_feedback,no_semaphore_feedback,no_event_feedback,no_query_feedback`
in the guest, the run passed 11 of 12 ([`zeus.md`](zeus.md#with-the-feedback-off-11-of-12)).

## How it was found

1. **The host makes the writes visible.** `src/host-fbtest/fbtest.c` is a
   native Vulkan program that copies what vkr does. It puts a buffer in each
   host-visible memory type, has the GPU write it with `vkCmdFillBuffer` and
   a transfer-to-host barrier, waits for the fence, and reads the result
   through three CPU views: `vkMapMemory`; an `mmap` of the exported
   dma-buf, as QEMU maps a blob into the hostmem BAR; and a write-back host
   page imported with `VK_EXT_external_memory_host`. On the 4060 and on
   the RTX 5090 every read in every type and view saw the write, 0 misses in
   200 rounds (`host/fbtest-zeus-rtx4060.txt`,
   `host/fbtest-helios-rtx5090.txt`). On the 5090 type 4 could not be
   allocated because other VMs had filled its 256 MiB BAR. Both drivers export
   host-visible memory as dma-buf, so vkr and QEMU take the same path on
   both hosts.
2. **The guest does not see them in type 2.** `src/guest-fbtest/fbguest.c`
   does the same in a Windows guest through Venus. It reads through the
   guest's mapping of the hostmem BAR. With fence feedback on, its
   first fence wait timed out, as the probes' did
   (`zeus/01-main-feedback-on/`). With `VN_PERF=no_fence_feedback`, so that
   its fence waits asked the renderer, the slot it read stayed 0
   in **198 of 200 rounds in type 2**. Neither
   `vkInvalidateMappedMemoryRanges` nor a 50 ms spin made the write appear.
   Types 3 (`HOST_CACHED`) and 4 (device-local, through the BAR) missed none
   (`zeus/02-main-no-fence-feedback/`).
3. **A stale cache line.** Three variants in type 2
   (`zeus/03-main-clflush/`):

   | Variant | Misses (x64, x86) |
   | --- | --- |
   | `primed`: the CPU writes 0 and reads it back, then the GPU writes (what Venus does with a slot) | 97/100, 100/100 |
   | `flushed`: the CPU writes 0 and `clflush`es the line, and does not read until the fence signals | 74/100, 81/100; some writes appear within 50 ms |
   | `clflush`: as `primed`, plus a `clflush` just before the final read | **0/100, 0/100** |

   The GPU's value is in memory. The guest reads a cached copy of the line
   that the GPU's write never invalidated: the GPU writes uncached system
   memory without snooping the CPU caches. In type 3 the writes are
   coherent with the CPU caches, so all three variants pass.
4. **Not the guest PAT.** The guest's kernel-mode driver maps this memory
   write-combined, as vkr reports (`VIRGL_RENDERER_MAP_CACHE_WC` for a
   coherent, uncached type). This QEMU (`d8a6048aae`) turns off KVM's
   ignore-guest-PAT quirk by default, and Zeus's kernel allows it:
   `-accel kvm,honor-guest-pat=on` starts. Which cache holds the stale line
   (an alias the host keeps cacheable, or the CPU's self-snoop on an
   uncached access) is not established. What matters for the fix is
   measured: on this host, guest reads of GPU-written type-2 memory are
   stale, and type 3 is correct.

Why helios's RTX 5090s work and the 4060 does not is not proven, because
neither driver can run on the other card (decision D3: no host system
updates). The difference that fits is the host CPU: helios is an AMD Ryzen
9 9950X and Zeus an Intel i7-13700H. Zeus's Intel iGPU writes coherently,
so Intel1 passes too.

## The fixes

`patches/mesa/0065-venus-feedback-buffers-in-host-cached-memory.patch`
(`vn_feedback_buffer_create`, `src/virtio/vulkan/vn_feedback.c`): the
feedback buffer takes the first memory type with `HOST_COHERENT` and
`HOST_CACHED`, and otherwise the first `HOST_COHERENT` type as before. On
NVIDIA that is type 3, on RADV the cached GTT type, and on ANV the cached
device-local type. Polling cached memory is also cheaper than polling uncached
memory. Query feedback buffers go through the same function.

`patches/mesa/0066-venus-disable-feedback-when-writes-do-not-arrive.patch`
(`vn_feedback_self_test`) protects hosts where even that type fails. After
`vkCreateDevice` it allocates a fence feedback slot, sets it to
`VK_NOT_READY`, and submits the fence feedback command (`vkCmdFillBuffer` of
`VK_SUCCESS` with its barriers) on a queue that can feed back, with a fence
that has no feedback of its own. It waits for that fence through the
renderer and checks the slot, allowing the write 20 ms to land. If the
write never lands, it logs "feedback writes by the host GPU do not reach
the guest" and sets `dev->feedback_disabled`. Fences, timeline semaphores,
events and queries created on that device then take the `VN_PERF`
`no_*_feedback` paths. Each status check then costs a renderer round trip,
but the device stays correct.

The first version of 0066 waited with `vkQueueWaitIdle`. vkr treats that
call as fatal ("no blocking call", `vkr_dispatch_vkQueueWaitIdle`), so every
Venus context died: vulkaninfo crashed and 6 of 12 checks passed
(`zeus/10-e2e-selftest-queuewaitidle/`, `qemu-log.txt`:
`vkQueueWaitIdle resulted in CS error`). The patch waits on a fence
instead.

Nothing changes on the host. A host-side change would not ship anyway:
the package uses the host's own virglrenderer (decision D3).

## Proof

| Run | Evidence (`docs/evidence/nvfb/`) | Result |
| --- | --- | --- |
| Zeus Nvidia1, e2e run 1, `6dabb22` (0065 + 0066), package defaults | `zeus/11-e2e-run1/` | **12 of 12**. D3D12 55 fps, Vulkan 58.5 fps, OpenGL 60 to 62 fps |
| Zeus Nvidia1, e2e run 2, the same | `zeus/13-e2e-run2/` | **11 of 12**: x86 OpenGL readback black ([below](#the-black-opengl-readback)) |
| Zeus Nvidia1, e2e run 3, the same | `zeus/16-e2e-run3/` | **12 of 12** |
| Zeus Nvidia1, fbguest with feedback on, `6dabb22` | `zeus/12-fixed-feedback-on/` | Fence waits work (Venus feedback now in type 3). Type 2 still misses for the test's own reads; 0067 later fixed that |
| Zeus Nvidia1, 0066 without 0065 (an experimental bundle), feedback on | `zeus/14-selftest-only-feedback-on/` | The self-test fallback works. fbguest's fence waits all complete, where `main`'s timed out (`01-main-feedback-on/`). Vulkan passes (56 fps), D3D12 passes at 51 fps, as fast as with `VN_PERF` set |
| Zeus Intel1, e2e, `6dabb22` | `zeus/15-e2e-intel1/` | **12 of 12** |
| helios AMD1, e2e, `6dabb22` | `helios/amd1-e2e/` | **12 of 12** |
| Zeus Nvidia1, e2e, `0b26b36` (`main` merged, 0065 to 0067 with the three-round check) | `zeus/40-e2e-0b26b36-a/` | **12 of 12** |
| helios Nvidia1 (RTX 5090), e2e, `0b26b36` | `helios/nv1-e2e-0b26b36/` | **11 of 12**: x64 D3D12 drew 1 frame in 20 s (readback correct, nothing on the console). Rerun in the same lab: 14 fps, then on a quieter card 41 to 46 fps. See [the D3D12 first run](#a-slow-first-d3d12-run) |
| helios Nvidia2 (RTX 5090), e2e, `bf4d948` (the committed 0067) | `helios/nv2-e2e-bf4d948/` | **12 of 12**. D3D12 3.7 and 9.4 fps and OpenGL 8.7 and 11.8 fps while another worker's benchmark VM shared the card |
| helios Nvidia1, e2e, the same tree without 0065 to 0067 (`src/diag/build.sh mainonly -0065 -0066 -0067`) | `helios/ab-mainonly-nv1-e2e/` | **12 of 12** (D3D12 42.5 and 21.4 fps, OpenGL 8.4 and 9.3 fps, so the slow OpenGL is not ours) |
| Both 5090 labs above, warm repeats back to back | `helios/ab-repeat/repeat.txt` | D3D12 x64 43.5 to 45.6 fps without 0065 to 0067, 44.2 to 45.7 with; x86 41.1 to 42.1 and 44.7 to 46.3; OpenGL 59.1 to 59.4 and 59.1 to 60.8. No difference |
| Zeus Nvidia1, e2e run 1, `bf4d948` | `zeus/43-e2e-bf4d948-1/` | **12 of 12**. D3D12 51.5 and 45.5 fps, OpenGL 53.3 and 51.4 fps |
| Zeus Nvidia1, e2e run 2, `bf4d948` | `zeus/45-e2e-bf4d948-2/` | RUN2 |
| Zeus Nvidia1, fresh lab, `main` without 0065 to 0067 | `zeus/42-mainonly/` | The original bug: fbguest's first fence wait times out, D3D12 fails 3 of 3 (`api_failure`), OpenGL hangs |
| Zeus Nvidia1, fresh lab, merged `main` with 0065 + 0066, without 0067 | `zeus/44-no67/` | x64 OpenGL black readback **5 of 6**; with 0067, 0 of 24 (`zeus/41-memlog2/`) |

## A slow first D3D12 run

In a fresh lab where D3D12 is the first API to run after fbguest, the
first D3D12 probe of each architecture presents one frame in 8 to 20
seconds (0.03 to 0.24 fps); every later run reaches 50 fps. This happens with
0065 and 0066 alone (`zeus/44-no67/`: 0.24 and 0.17 fps), with 0067
(`zeus/41-memlog2/`: 0.04 and 0.03 fps) and with 0066 alone
(`zeus/14-selftest-only-feedback-on/`), so it is not 0067. In the
acceptance runs D3D12 comes after D3D9 and D3D11 and normally does not
stall. The one exception was x64 on helios Nvidia1 (`helios/nv1-e2e-0b26b36/`),
the only 5090 failure. Its readback was correct; one frame in 20 s left
nothing for the console captures. Its cause is not investigated; a cold
vkd3d-proton shader cache in the new lab fits.

## The black OpenGL readback

In about one OpenGL probe run in three on Zeus Nvidia1, `glReadPixels` of
the back buffer returned 0, 0, 0 at all three markers. The clear colour is
not black, so the readback held no rendered data at all. The same run
rendered more than 1,000 frames at about 62 fps and showed them on the
console. This is older than 0065: it happened with all feedback off on
2026-10-19 ([`zeus.md`](zeus.md#with-the-feedback-off-11-of-12)), and it
did not happen on the RTX 5090s, AMD1 or Intel1. Repeated in a kept lab
with 0065 and 0066 (`zeus/20-gl-repeat/repeat.txt`, 8-second runs):

| Setting (probe process only) | Black readbacks |
| --- | --- |
| none, x86 | 1 of 8 |
| none, x64 | 4 of 8 |
| `ZINK_DEBUG=flushsync` | 2 of 8 |
| `ZINK_DEBUG=noreorder` | 5 of 8 |
| `ZINK_DEBUG=sync` (a full memory barrier before every copy, blit and draw) | **8 of 8** |
| `ZINK_DEBUG=noreorder,sync` | 3 of 6 |

It is neither feedback nor Zink's command reordering.

**Which memory.** A diagnostic build (`src/diag/0099-venus-nvfb-memlog.patch`,
built by `src/diag/build.sh`; not in the package) logs every Venus
`vkAllocateMemory`. Under `ZINK_DEBUG=sync` the GL probe allocates
(`zeus/30-memlog/gl/memlog-sync.txt`):

| Allocation | Type |
| --- | --- |
| The readback staging buffer, 2,621,440 bytes (1024 × 640 × 4) | 3, cached |
| Four 2 MiB buffers: Zink's stream and upload buffers (`PIPE_USAGE_STREAM`, `HOST_VISIBLE` only) | **2, uncached** |
| Images and device-local buffers | 1 |

So the readback buffer the CPU reads is in the cached type, and the GPU's
copy into it is visible. The data the CPU writes for the GPU (vertices and
constants, including the blit that `glReadPixels` uses to convert the back
buffer into the staging texture) goes through type 2. The same coherence
gap applies in that direction: the guest's write stays in a CPU cache line
that the GPU's read does not snoop, so the GPU can read stale data. A blit
drawn from stale vertices writes nothing, and the staging buffer stays
zero. `ZINK_DEBUG=sync` adds barriers that change the timing so that the
blit's few bytes are always still in the cache.

**The fix.** `patches/mesa/0067` extends 0066's check to every uncached
`HOST_COHERENT` type that has a cached counterpart: same heap, and a
superset of the flags plus `HOST_CACHED` (NVIDIA type 2, counterpart type
3). The check puts a 4 KiB buffer in that type, writes and reads its first
word on the CPU, and has the GPU fill it in the same submission as the fence
feedback check. If the fill is not visible after the fence and 20 ms, every
later allocation of that type on the device is backed by the cached type.
Exported memory and dedicated allocations whose resource cannot use the
cached type are not remapped. Both types are `HOST_COHERENT`, so the
application's view does not change; the host GPU then accesses the memory
coherently with the CPU caches, in both directions.

A stale read needs the line to still be in the cache when the GPU writes.
The first version checked once after a blocking fence wait. In 2 of 10 GL
processes, and in one fbguest device, the line had been evicted by then,
so the check passed and nothing was remapped (`zeus/31-r67/`). Three rounds
fixed the detection (24 of 24 processes, `zeus/32-r67b/`) but cost too
much on a busy host. On helios, with ten VMs running, one fence round trip
took 5 to 10 ms (`helios/nv1-fbguest-0b26b36/`). The check now runs one
round and does not block. It polls the fence and reads the test words on
every iteration, so their lines are cached when the GPU writes. On Zeus
Nvidia1 (`zeus/41-memlog2/`, the committed 0067 plus the allocation log):

| Run (6 GL, 3 D3D12 each) | Black readbacks | Processes with type 2 backed by type 3 |
| --- | --- | --- |
| x64 OpenGL, `ZINK_DEBUG=sync` | 0 of 6 | 6 of 6 |
| x64 OpenGL, defaults | 0 of 6 | 5 of 6 |
| x86 OpenGL, `ZINK_DEBUG=sync` | 0 of 6 | 6 of 6 |
| x86 OpenGL, defaults | 0 of 6 | 6 of 6 |
| x64 and x86 D3D12 | 0 of 6 failed | 6 of 6 |

Detection is 29 of 30. The one process the check missed still read back
correctly. fbguest's own type-2 buffer, now backed by type 3, saw every GPU
write in all three variants (x64 and x86).

Startup cost. fbguest takes the median of 11 `vkCreateDevice` calls:

| Host, bundle | x64 | x86 |
| --- | --- | --- |
| Zeus Nvidia1, 0065 + 0066 (`zeus/12-fixed-feedback-on/`) | 64 ms | 62 ms |
| Zeus Nvidia1, 0067 with three rounds (`zeus/32-r67b/`) | 112 ms | 95 ms |
| Zeus Nvidia1, committed 0067 (`zeus/41-memlog2/`) | 74 ms | 71 ms |
| Zeus Nvidia1, 0065 + 0066 on merged `main`, without 0067 (`zeus/44-no67/`) | 68 ms | 74 ms |
| Zeus Nvidia1, `main` without 0065 to 0067 (`zeus/42-mainonly/`) | 62 ms | 60 ms |

On a host where the writes arrive, the check is one more 4 KiB allocation
and mapping in the same single submission and fence poll as 0066. The
timing on helios could not be isolated: with ten VMs on the host,
`vkCreateDevice` alone varied from 154 to 657 ms between calls
(`helios/nv1-fbguest-0b26b36/`).

## What remains

- 0065 to 0067 belong upstream in Mesa's Venus driver.
- Why this host's GPU writes bypass the guest's cache coherence (an alias
  the host keeps cacheable, or the CPU's self-snoop on uncached accesses) is
  not established. KVM or the host's virglrenderer could fix it for every
  guest driver; the package cannot change either (decision D3).
- Yttrium's D3D9/D3D10/D3D11 driver has its own Venus transport and is not
  covered by 0067. It passed every check on Zeus Nvidia1.

## Files

| Path | What it is |
| --- | --- |
| `src/host-fbtest/fbtest.c`, `build.sh` | The native host test (built in the Arch image pinned in `patches/host/PINS`, for generic x86-64) |
| `src/guest-fbtest/fbguest.c`, `build.sh` | The guest test (shared llvm-mingw toolchain), with the `primed`, `flushed` and `clflush` variants and a median of 11 `vkCreateDevice` calls |
| `src/fbguest-lab.sh` | Brings up `win3d-nvfb-<gpu>`, installs a bundle, runs fbguest x64 and x86 in the autologon session with `VN_PERF` set for the test only |
| `src/repeat-probe.sh` | Runs one probe N times in a kept lab with extra environment variables |
| `src/diag/0099-venus-nvfb-memlog.patch`, `src/diag/build.sh` | The allocation log (`VN_NVFB_MEMLOG=<file>`), and the script that builds a bundle with extra or fewer Mesa patches in a scratch copy of the tree (the memlog, r67 and 0066-only bundles) |
| `host/` | fbtest output: Zeus RTX 4060 and Intel Iris Xe, helios RTX 5090 |
| `zeus/` | Guest runs and e2e evidence, numbered as in the tables above |
