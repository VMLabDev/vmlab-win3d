# "can't alloc VA space for mapping": the RTX 5090's BAR1 runs out

From 02:02 on 2026-10-19 helios's kernel logged
`NVRM: dmaAllocMapping_GM107: can't alloc VA space for mapping.` many times.
In the same period, Venus rings in our Windows guests on NVIDIA timed out
in `vkGetImageSubresourceLayout`, and one acceptance run failed 10 of 12
(shaders2's `after/nvidia1-attempt1`). This page names the cause, gives the
numbers and records what is fixed and what is not. Investigated on
2026-10-19 on helios with NVIDIA 610.57.04, vmlab 0.28.0-alpha, the patched
QEMU from `patches/host/PINS` and virglrenderer 1.3.0. The evidence is in
`docs/evidence/nvva/`.

## Result

| Item | Result |
| --- | --- |
| What runs out | The card's **BAR1 aperture**: 256 MiB on each RTX 5090, because helios has resizable BAR off. It is not a leak, and it is not a GPU virtual address space. The kernel's second assertion names it: `reusemappingdbMap(&pBar1VaInfo->reuseDb, …) @ kern_bus_gm107.c` |
| Scope | **Per card.** Every process on the card shares BAR1: the host desktop (on `Nvidia1`), every render server and every QEMU. The log lines carry no PID and no PCI address |
| What fills it | NVIDIA offers BAR1 to Vulkan as memory type 4 (`DEVICE_LOCAL \| HOST_VISIBLE \| HOST_COHERENT`, heap 2, 246 MiB), and Venus passes the type to the guest unchanged. A guest blob of it costs **twice its size**: once in the render server's `vkAllocateMemory`, and again in the dma-buf `mmap` that virglrenderer makes in QEMU when the guest maps it. The largest user is the Yttrium D3D UMD, which places OPTIMAL shared colour images there (dwm and every D3D window). Vulkan clients (DXVK, vkd3d-proton, Zink) use the type as an upload heap |
| Cost per Windows VM | About **40 MiB idle** at the desktop, and about 60 MiB while it runs the probes (three VMs: 157 MiB idle, 35 MiB with none). The host desktop on `Nvidia1` takes more on top |
| How it fails | (1) The driver admits a heap-2 allocation only while the card-wide heap has room, and the guest sends `vkAllocateMemory` asynchronously, so it does not see a refusal. The next bind names memory the host never created (`vkr: failed to look up object N of type 8`, a CS error), the Venus context dies, and the next synchronous call times out. (2) An allocation is admitted, but its dma-buf `mmap` fails with `ENOMEM`. Only this second case logs `can't alloc VA space for mapping` |
| Safe VM count, no host change | **Two Windows VMs per RTX 5090.** Three VMs that only loop the probes already logged 62 errors in 5 minutes on `Nvidia2`, the card without the desktop, and the 02:02 to 03:05 bursts came with three VMs on each card. On `Nvidia1`, two VMs plus the desktop reached 141 MiB idle |
| Fix in the package | `patches/mesa/0070` (Venus ICD) reports a device-local, host-visible type on a heap less than half the size of the largest device-local heap as device-local only. Vulkan, DXVK, vkd3d-proton and Zink then keep their host-visible memory in system memory. Zink needs `patches/mesa/0086` (written by the unigl worker, carried unchanged) to work without such a type. Resizable-BAR hosts and integrated GPUs (`AMD1`, Intel) are unchanged. This removes the Vulkan-API clients' share. It does **not** remove Yttrium's share, so the safe count stays two |
| Not fixable in the guest (today) | Moving Yttrium's shared images to system memory (a draft, 0071, now dropped) broke dwm. NVIDIA cannot import a system-memory image dma-buf into another process, and dwm imports every window's surface ([0071](#why-0071-was-dropped)) |
| Real fix | **Turn on resizable BAR in helios's firmware.** BAR1 then covers the whole 32 GiB of VRAM, as on Zeus's laptop (`docs/zeus.md`). This is a host firmware change. Decision D3 rules out host changes during this work, so it is a recommendation for the user |

## How it was found

1. **Timeline** (`03-real-world/va-bursts-vs-nvidia-labs.txt`, from
   `src/timeline.py`). Every burst from 02:02 to 03:05 fell while each RTX
   5090 carried three Windows VMs (`d3d12app-nv1`, `n5-w11-nv1` and
   `wu-nvidia1` on `Nvidia1`; `e2e-windows-11-nvidia2`, `ffp2-nv2` and
   `ffp2-w10-nv2` on `Nvidia2`).
2. **BAR1 is the heap.** `nvidia-smi -q -d MEMORY` gives 256 MiB of BAR1 on
   each card. Vulkan's `VK_EXT_memory_budget` reports usage for heap 2 that
   matches `nvidia-smi`'s BAR1 Used, card-wide: 142 MiB on `Nvidia1` and
   35 MiB on the empty `Nvidia2`.
3. **Live correlation** (`02-passive/`, 04:58 to 05:30, ten-second samples
   of both cards while other workers ran four or five VMs on `Nvidia1`).
   `Nvidia1` stayed at 228 to 254 MiB. Every new burst of VA lines came
   while it was at **236 MiB or more**; `Nvidia2` peaked at 207 MiB and
   produced none. The count went from 161 to 526 lines in that half hour.
4. **On the host, without a VM** (`01-host-probe/`, `src/bar1probe.c`), on
   the empty `Nvidia2`:

   | Test | BAR1 |
   | --- | --- |
   | 4 × 16 MiB of type 4, allocated | +65 MiB |
   | the same, also `vkMapMemory` | +65 MiB |
   | the same with dma-buf export and `mmap` of the fd (the Venus path) | **+129 MiB** |
   | type 4 in 4 MiB dma-buf chunks until refused | `vkAllocateMemory` −2 at 245 MiB, no kernel line |
   | 30 + 30 + 30 MiB, then 20 MiB, dma-buf | the last `mmap` fails with `ENOMEM`, BAR1 at 237 MiB, and the kernel logs **15 lines of exactly helios's message** |
   | type 2 or 3 (system memory), dma-buf and `mmap` | +0 |
   | type 1 (device-local only), exported, no `mmap` | +0 |
   | one VkDevice per process | about 1 MiB each |

5. **The guest symptom.** In `03-real-world/dwmheap-nv1a-qemu-render-server.txt`
   (another worker's `Nvidia1` lab while that card sat near full), render-server
   contexts died one after another with `failed to look up object 31 of type
   8` and `vkBindImageMemory resulted in CS error`. Type 8 is
   `VkDeviceMemory`. Shaders2's timeouts were `vkGetImageSubresourceLayout`
   on a `display-image`, the command that follows the asynchronous allocate
   and bind in Yttrium's display-image path.

The same type-8 CS error also appears with no BAR1 pressure. dwm failed its
`import-display-image` every minute with the dropped 0071 at 37 to 79 MiB.
So the message alone does not prove BAR1 exhaustion: check `nvidia-smi`'s
BAR1 Used, or the kernel log, as well.

## Controlled reproduction

Three Windows 11 labs on `Nvidia2` (reserved for this run), made with
`src/load.sh setup`. Each stage runs `src/stage.sh`: the ten probes in a loop
in each lab for five minutes, optionally with a host process that holds
BAR1 (`bar1probe`, standing in for a fourth tenant, because the shared rules
cap our VMs at three per card). `src/load.sh monitor` samples BAR1 every
10 s, so short peaks are missed. The same labs were upgraded in place
between bundles and rebooted, and the guest's `viogpu_d3d10_x64.dll` hash
was checked after each upgrade.

| Stage | Unpatched (`06-unpatched/`) | 0070 and 0071 (`04-baseline/`) |
| --- | --- | --- |
| 3 VMs booted, idle | 157 MiB | 64 MiB |
| 1 VM under load | peak 195 MiB, 0 lines | peak 70 MiB, 0 lines |
| 2 VMs under load | peak 203 MiB, 0 lines | peak 71 MiB, 0 lines |
| 3 VMs under load | peak 213 MiB, **62 lines** | peak 79 MiB, **0 lines** |
| 3 VMs + 82 MiB host tenant | the tenant is refused (`vkAllocateMemory` −2), 38 lines | the tenant fits; peak 243 MiB, 4 lines |
| 3 VMs + 86 MiB host tenant | not run | peak 251 MiB, 4 lines, dwm lost its context about 20 times per VM |

Every probe passed in every stage. The probes create few shared surfaces;
dwm and the BAR1 counters show the effect. `04-baseline/` is misnamed: by
mistake, that bundle already carried 0070 and 0071 (its build retried after
a download failure and picked up the new patches). So it is the patched
column, and `06-unpatched/` is the real baseline.

With 0070 alone (the shipped fix, `07-0070/`), two VMs at the desktop and a
third booting put `Nvidia2` at 120 MiB, much as unpatched. Yttrium's share
dominates an idle desktop.

## Why 0071 was dropped

0071 applied 0070's rule to the Yttrium winsys, so shared display images
went to system memory. Under it, every lab's dwm failed
`opened display allocation Venus import failed` and lost its Venus ring
about once a minute from first boot, at low BAR1 (`04-baseline/yttrium-errors-*.txt`).
None of that happened once the same labs ran the unpatched bundle. The unigl
worker saw the same on `Nvidia1`. The host test `src/importtest.c` shows
why (`01-host-probe/importtest-*.txt`). Another process can import a
type 1 (VRAM) dma-buf for a LINEAR 1280x800 image and bind it, but importing
a type 2 or 3 (system memory) one fails with −2 (the fd allows types 0 and 3
only). dwm runs in its own Venus context and render server, so it is that
other process. LINEAR images cannot use type 4 at all (their
`memoryTypeBits` are `0xf`), so Yttrium's BAR1 use is its OPTIMAL shared
images.

## What remains

- **Yttrium's shared images** stay in BAR1. They could move to type 1
  (VRAM without host access) when nothing maps them on the CPU. That needs a
  per-resource decision in the winsys (which surfaces the guest maps) and a
  new round of e2e runs on every GPU. Until then, keep **two Windows VMs per
  RTX 5090**, or turn on resizable BAR.
- **Asynchronous allocation hides the refusal.** A refused
  `vkAllocateMemory` becomes a CS error at the next command and kills the
  context. The unigl worker's draft 0085 makes allocations from such a heap
  synchronous in the Venus ICD. Yttrium has the same pattern in its own
  winsys.
- **Proof on AMD1.** 0070 changes nothing on `AMD1`, because its
  device-local, host-visible type sits on the largest device-local heap
  (`01-host-probe/host-memory-types-radeon.txt`). No AMD1 run was
  made for this page, and Nvidia2's was the only reservation.

## Proof

`tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab win3d-nvva-e2e`
with the 0070 and 0086 package, while labs `a` and `b` (same package) ran
the probe loops (`07-0070/load-during-e2e/`). The result is in
`docs/evidence/acceptance/windows-11-nvidia2/` and below.

## Evidence

| Path under `docs/evidence/nvva/` | What |
| --- | --- |
| `src/bar1probe.c` | Host BAR1 probe: allocate, map, dma-buf, export, import |
| `src/importtest.c` | Cross-process dma-buf import into a LINEAR image |
| `src/load.sh`, `src/stage.sh` | Lab setup, probe loops, BAR1 monitor, stages |
| `src/timeline.py` | VA-line bursts against the vmlab lab-daemon logs |
| `01-host-probe/` | Host probe outputs |
| `02-passive/` | Both cards sampled during other workers' loads |
| `03-real-world/` | Kernel log, timeline, dwmheap's render-server lines |
| `04-baseline/` | Stages with 0070 and 0071 (misnamed, see above); `monitor.txt` covers every stage on `Nvidia2` |
| `06-unpatched/` | Stages with the unpatched package |
| `07-0070/` | The shipped package: installs, the load during the e2e run |
