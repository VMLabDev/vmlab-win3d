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
| Owner | The guest Venus driver chose the memory type. The underlying behaviour belongs to the host platform, an Intel CPU host with this NVIDIA GPU. On helios (an AMD CPU) the RTX 5090s pass with feedback in type 2; fbguest was not run in a helios guest |
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
| Zeus Nvidia1, e2e run 2, the same | `zeus/13-e2e-run2/` | **11 of 12**: x86 OpenGL readback black ([below](#the-black-opengl-readback)); every Vulkan, D3D12 and the other OpenGL check pass |
| Zeus Nvidia1, fbguest with feedback on, `6dabb22` | `zeus/12-fixed-feedback-on/` | Fence waits work (Venus feedback now in type 3). Type 2 still misses for the test's own reads: that is the host's behaviour, and no package component reads GPU-written type-2 memory |
| Zeus Nvidia1, 0066 without 0065 (an experimental bundle, built from this branch with 0065 removed), feedback on | `zeus/14-selftest-only-feedback-on/` | The self-test fallback works. fbguest's fence waits all complete, where `main`'s timed out (`01-main-feedback-on/`). The Vulkan probe passes (56 fps, x64 and x86), and D3D12 passes at 51 fps, as fast as with `VN_PERF` set. The first D3D12 run of each architecture in the fresh lab ran at only 0.06 and 0.18 fps (x64 also read back only the clear colour); later runs did not. OpenGL hit the black readback below |
| helios AMD1, `tests/e2e/run.sh --template windows-11`, `6dabb22` | `helios/amd1-e2e/` | **12 of 12** |
| Zeus Nvidia1, e2e run 3, the same | `zeus/16-e2e-run3/` | **12 of 12** |
| helios Nvidia1, e2e | — | **Not run**: Nvidia1 stayed at the two-VM limit for our labs (the 2026-10-19 BAR1 rule) until the time box ran out |
| Zeus Intel1, `tests/e2e/run.sh --template windows-11 --host-gpu Intel1`, `6dabb22` | `zeus/15-e2e-intel1/` | **12 of 12** |

Startup cost of 0066: when the check passes it is one submission and one
fence wait, inside a `vkCreateDevice` that takes about 50 to 90 ms in these
guests anyway. On Zeus Nvidia1, where the check passes with 0065,
fbguest's median `vkCreateDevice` over 11 calls was 62 to 64 ms (minimum 55
to 57 ms) with 0065 and 0066 (`zeus/12-fixed-feedback-on/`), against 46 to
91 ms for single calls on `main` (`zeus/01-` to `03-`). The difference is
within the noise. The check takes the same path on any host where it
passes. No separate timing was taken on helios.

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
The first version checked once, and in 2 of 10 GL processes and in one
fbguest device, the line had been evicted, so the check passed and nothing
was remapped (`zeus/31-r67/`). The check now re-reads the words just
before each submission and runs up to three rounds, stopping at the first
stale read. With that (`zeus/32-r67b/`):

| Run (Zeus Nvidia1, 6 each) | Black readbacks | Processes with type 2 backed by type 3 |
| --- | --- | --- |
| x64, `ZINK_DEBUG=sync` | 0 of 6 | 6 of 6 |
| x64, defaults | 0 of 6 | 6 of 6 |
| x86, `ZINK_DEBUG=sync` | 0 of 6 | 6 of 6 |
| x86, defaults | 0 of 6 | 6 of 6 |

fbguest's own type-2 buffer now sees every GPU write as well (0 misses in
all three variants, x64 and x86), because its allocation is backed by type
3.

On a host where the check fails (Zeus Nvidia1) it costs the failed round's
20 ms wait plus the extra buffer: fbguest's median `vkCreateDevice` went
from 62 to 64 ms to 95 to 112 ms (`zeus/32-r67b/fbguest-*.txt`). On a host
where the writes arrive it is three short rounds; HELIOSCOST

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
