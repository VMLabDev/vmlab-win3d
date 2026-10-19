# dwm heap corruption in the first boot after the install (NVIDIA)

`docs/d3d12app.md` reported that on NVIDIA, in the first boot after the
install, `dwm.exe` crashed with heap corruption (`0xc0000374`, with
`viogpu_d3d10.dll` loaded) while the first D3D12 sample ran. One more reboot
cleared it, and the cause was open. This page gives the cause, the fix and
the evidence, and lists what is still unproven.

## Result

- **Reproduced** in a fresh `Nvidia1` lab, in the first boot after the
  install, with our own D3D12 probe as the first program: dwm died with
  `0xc0000374` at the same `ntdll.dll` offset (`0x117eb5`) as in the
  d3d12app report. A WER full dump was captured.
- **Cause:** a double free in our D3D10/11 UMD. The D3D11 runtime calls the
  DXGI DDI `pfnTrimResidencySet` from a thread-pool thread when the kernel
  sends a trim notification. Our `TrimResidencySetWDDM2_0()` flushed the
  device's threaded context on that thread. The flush ran a queued batch
  outside the thread that owns the context. The batch's flush-label callback
  freed a record that had already been freed, and the heap stopped dwm.
- **Fix:** `patches/mesa/0060-d3d10umd-trim-residency-set-leaves-the-context-alone.patch`.
  `TrimResidencySet` is advisory, and it now returns `S_OK` without touching
  the pipe context.
- **Why NVIDIA and the first boot:** dwm was recovering from a lost device
  when the trim notification arrived. The device was lost because Venus
  contexts in that boot went into a fatal decoder state on the host (see
  [The trigger](#the-trigger)). Worker `nvva` traced such failures on the
  RTX 5090s to their shared 256 MiB BAR1: a busy first boot (many processes
  holding Venus blobs) and other VMs on the same card exhaust it. That part
  is an inference from the matching host log lines, not proven here.

## The crash

`repro-nvidia1/` is the reproduction, in lab `win3d-dwmheap-nv1a` on
`Nvidia1`, from a bundle built at `b198918` (before the fix):

1. The lab was set up by `src/lab-prep.sh`: WER `LocalDumps` (full) for
   `dwm.exe`, then the autologon desktop.
2. `scripts/install-into-vm.sh` installed the bundle.
3. In the first boot after the install's final reboot, `d3d12_probe.exe`
   was the first program run. It failed: its first GPU fence did not signal
   within 10 s (`app-errors-and-probe.txt`).
4. dwm crashed at 11:38:21 guest time with `0xc0000374` at `ntdll.dll` +
   `0x117eb5`, the same offset as in `docs/evidence/d3d12app/nvidia1-first-boot/app-errors.txt`.

cdb (WinDbg 1.2610.1001.0, Microsoft's public symbols) on the dump
(`cdb-dwm-stacks.txt`, `cdb-heap-and-trim.txt`):

```
Error type:    HEAP_FAILURE_SEGMENT_LFH_DOUBLE_FREE

ntdll!RtlFreeHeap+0x231
ucrtbase!free_base+0x1b
viogpu_d3d10!D3DKMTConfigureSharedResource+0x1c743   tc_batch_execute (TC_CALL_callback)
viogpu_d3d10!D3DKMTConfigureSharedResource+0xfa16    _tc_sync
viogpu_d3d10!D3DKMTConfigureSharedResource+0x1290a   tc_flush
viogpu_d3d10!OpenAdapter+0x32a55                     TrimResidencySetWDDM2_0
d3d11!NDXGI::CDevice::TrimNotificationCallback+0x82
DXCore!DxTrimWnfCallback+0xdd
ntdll!RtlpWnfNotificationThread+0x87
```

The right-hand names come from the unstripped build of the same DLL
(`out/mesa/.work/build-x64/.../viogpu_d3d10.dll`): the stripped DLL keeps
only its exports, so cdb names each frame after the nearest export. The
call at the return address is `TC_CALL_callback`, `p->fn(p->data)`. The only
threaded-context callback this UMD queues is
`yttrium_threaded_set_flush_label`, which ends in `FREE(queued)` and is
tail-called into `free`. That is why `free_base` returns straight into
`tc_batch_execute`.

At the same time, in the same dwm:

- the compositor thread was in `dwmcore!CDeviceManager::ProcessDeviceLost`,
  releasing the D3D11 device;
- the uDWM event thread was in `CGraphicsDeviceManager::HandleGraphicsDeviceLost`,
  creating a new one;
- the three threaded-context worker threads (one per dwm device) were idle.

`d3d11!NDXGI::CDevice::TrimNotificationCallback` reads a flag in the device.
Only when it is set does it take the layered-device lock and the
free-threaded DDI lock before it calls `pfnTrimResidencySet`
(`cdb-heap-and-trim.txt`). So the runtime may call the DDI with no lock held,
on a thread that does not own the device. A Gallium threaded context has one
producer thread. A flush from any other thread can run a batch that the
owner is still writing, or run it twice.

## The trigger

The device loss came from the host side. In the same boot, the render
server log shows Venus contexts going into a fatal decoder state
(`qemu-log-render-server.txt`):

```
vkr: failed to look up object 26 of type 8
vkr: vkBindImageMemory resulted in CS error
vkr: ring_submit_cmd: vn_dispatch_command failed
```

Object type 8 is `VkDeviceMemory`. The guest bound an image to memory that
the host never created, because the asynchronous `vkAllocateMemory` (an
import of a blob resource) had failed on the host. The context is dead after
that. Its fences never signal: the D3D12 probe's fence timed out, and dwm's
UMD reported the device removed. Fatal contexts kept appearing in the log
after that, about one a minute, while `ShellExperienceHost.exe` restarted
again and again. Which process owned each context is not in the log.

The d3d12app reports and this repro fit the BAR1 finding of worker `nvva`.
Both RTX 5090s have a 256 MiB BAR1 with no Resizable BAR, shared by every
VM on the card. Blob mappings fail when it runs out, and the guests then see
exactly these CS errors. A first boot runs more programs that hold Venus
blobs (Start menu, OneDrive setup, widgets, and so on) than later boots do,
and the d3d12app labs and this one shared their card with other labs. The
repro lab shared `Nvidia1` with two other VMs. A second lab, started at the
same time with PageHeap on dwm, had no CS errors and no crash. The fix for
the BAR1 exhaustion belongs to `nvva` (Mesa patches 0070 and 0071).

The UMD fix does not stop the device loss. It stops dwm from dying while it
recovers from one, so dwm rebuilds its device as Windows intends.

## The fix

`patches/mesa/0060` changes one function in
`src/gallium/frontends/d3d10umd/Device.cpp`:

```c
static HRESULT APIENTRY
TrimResidencySetWDDM2_0(DXGI_DDI_ARG_TRIMRESIDENCYSET *args)
{
   /* Trim is advisory, and it must not touch the pipe context. ... */
   (void)args;
   return S_OK;
}
```

The old body called `yttrium_gdi_flush_labeled()`, to submit outstanding
work so retired upload allocations could be released. Nothing depends on
that. Live blob backing stays resident for its resource's lifetime anyway,
and the owner thread's next `Flush` or `Present` submits the same work.

Other fixes were considered and not taken:

- **A forced reboot or a dwm restart in the installer.** The installer
  already reboots after it installs the driver. The first boot after the
  install is a clean boot with the new driver, so dwm does not hold any old
  UMD state. The crash is in the UMD, and it can happen in any boot where
  dwm loses its device and gets a trim notification.
- **Flushing only when the caller is the owner thread.** The UMD does not
  track which thread owns a device, and the flush has no purpose that needs
  it.

## Evidence

All under `docs/evidence/dwmheap/`:

| Path | What |
| --- | --- |
| `src/lab-prep.sh` | Fresh lab from `labs/probe/vmlab.wcl` with a NAT NIC, WER full dumps of `dwm.exe` to `C:\dumps`, optional PageHeap for dwm, and the autologon desktop |
| `src/first-boot-run.sh` | One proof run: `lab-prep.sh`, the install, then in the first boot the D3D12 probe five times and the five samples; collects crashes, dumps and the render-server log |
| `fixed-nvidia1-a/`, `fixed-nvidia1-b/`, `fixed-amd1/`, `fixed-nvidia2/` | Proof runs with the fix (`src/first-boot-run.sh`): install log, five D3D12 probe results, the five samples (`samples/`, with console captures), Application Error events, `C:\dumps`, dwm's start time against boot time, and the render-server log |
| `e2e/` | `tests/e2e/run.sh --template windows-11` runs for this branch: `windows-11-amd1/`, and `windows-11-nvidia2-run1/`, `-run2-overloaded/` and `-run3/` |
| `src/f7-heaven.sh`, `src/pageheap.sh`, `f7-amd1/` | Compat finding F7: five Unigine Heaven D3D11 runs on AMD1 under full PageHeap with WER dumps, through `tests/apps/compat/run.sh` (`pageheap.sh` is unid3d's, copied) |
| `repro-nvidia1/` | The reproduction before the fix: cdb stacks and heap analysis of the dwm dump, the d3d11 `TrimNotificationCallback` disassembly, the Application Error events and the probe's result, and the render-server log |

The dump itself (445 MB, sha256
`6b75d24b92b9fcef19bfb364c2c84f1c0ac5744d4577425df94d23f37ae0f24b`) is not
committed.

## Proof runs with the fix

Each run is `src/first-boot-run.sh` on a fresh lab from
`x86_64/windows-11@26100.1742.5`, with package 0.1.1. The bundle was built
from `b198918` with `0060` applied before it was committed (its manifest
names `b198918`); `f7c73b6` committed the same patch unchanged. The x64
`viogpu_d3d10.dll` in it has sha256 `8f979e3c6d97ea81ab585b7a4e1551b3c04e5395bb70c2e6d16207d1757671f1`. Every program ran in the first boot after the
install's final reboot. Nothing else ran before it.

| Run | GPU | D3D12 probe, 5 runs | Five samples | dwm | Fatal Venus contexts in `qemu.log` |
| --- | --- | --- | --- | --- | --- |
| `fixed-nvidia1-a/` | Nvidia1 | 5 pass | 5 exit 0; `D3D12HelloTriangle` renders on the console | no crash, no dump; dwm is the one started at boot | 9 |
| `fixed-nvidia1-b/` | Nvidia1 | 5 pass | 5 exit 0 | no crash, no dump; dwm is the one started at boot | 0 |
| `fixed-amd1/` | AMD1 | 5 pass | 5 exit 0 | no crash, no dump; dwm is the one started at boot | 0 |
| `fixed-nvidia2/` | Nvidia2 | 5 pass | 5 exit 0 | no crash, no dump; dwm is the one started at boot | 1 |

Runs a, b and the AMD1 run used the bundle described above. The Nvidia2
run, the e2e runs and the F7 runs used the bundle built by
`scripts/build.sh` at `35fa4e9`: main at `b74ea3c` (with nvva's `0070` and
`0086`) merged with this branch.

`D3D12ExecuteIndirect` still shows only its clear colour, as
`docs/d3d12app.md` explains. That is a separate limit. No run had any
Application Error event.

Run a is the one that matters most. Its boot had the same host-side
context failures as the repro, so it ran under the same conditions, and dwm
survived. Runs b and AMD1 had no context failures, and the Nvidia2 run had
one, so they exercise the trigger little or not at all. The probe passed in
all twenty runs. In the repro, the probe failed because its own context was
among the dead ones.

### End-to-end runs

`tests/e2e/run.sh --template windows-11` at `35fa4e9`:

| Run | Result | Notes |
| --- | --- | --- |
| AMD1 | **12 of 12** | `e2e/windows-11-amd1/` |
| Nvidia2, run 1 | 11 of 12 | The x86 D3D12 probe passed its readback but presented one frame in 24 s, and its window was in no console capture. This is the known x86 D3D12 stall that worker x86d3d12 is chasing, also seen on AMD1 before this change |
| Nvidia2, run 2 | 5 of 12 | Most probes failed with device removed (`0x887a0005`, fence timeouts). Two other workers' labs had started on Nvidia2, so the card carried three VMs, over the limit of two. Not counted |
| Nvidia2, run 3 | **12 of 12** | Run with the card's slot taken through `gpuslot.sh`. `e2e/windows-11-nvidia2-run3/` |

### F7: Heaven D3D11

Worker unid3d reproduced compat finding F7, intermittent heap corruption in
Unigine Heaven's D3D11 renderer on AMD1, under full PageHeap in 2 of 5 runs.
Both crashes are a use-after-free of a `yttrium_threaded_flush_label`
record. `tc_batch_execute` ran it on the threaded context's worker thread
after the record had been freed (`docs/evidence/unid3d/f7-pageheap/`). That
is the same callback record as in the dwm crash. A batch that a foreign
thread's trim flush ran inline, and that the owner thread then submitted to
the worker, would run its callbacks twice: the first run frees the record,
and the second, on the worker, reads freed memory.

With `0060`, `src/f7-heaven.sh` ran Heaven D3D11 five times on AMD1 under
full PageHeap (`f7-amd1/try1` to `try5`). All five exited cleanly after
WM_CLOSE (4.5 to 10.1 fps by OCR of the on-screen counter), and `C:\win3d\dumps` stayed empty. F7 is most
likely fixed by `0060`. This is not proven. That Heaven gets trim
notifications was not observed, and at the old rate of 2 crashes in 5, five
clean runs in a row would happen by chance about 8 % of the time.

## What remains

- **F7 needs more runs to be certain.** Ten or more clean PageHeap runs
  would bring the chance of a false all-clear under 1 %.
- **No deterministic trigger.** The crash needs dwm to lose its device and
  get a trim notification. Neither can be forced from a test program:
  `IDXGIDevice3::Trim` does not call `pfnTrimResidencySet` (it trims the
  runtime's own memory and calls `Flush`), and the trim notification is the
  kernel's. Publishing the DXCore trim WNF state from a SYSTEM process might
  force it. That was not tried.
- **The lost device itself.** Venus contexts dying with
  `failed to look up object N of type 8` is the host-side BAR1 exhaustion
  that `nvva` owns. The guest also has a weakness here:
  `vkAllocateMemory(import-display-image)` and the other image paths in
  `yttrium_venus2_resource.c` submit the allocation asynchronously and bind
  without checking that it worked. A failed import therefore kills the whole
  context instead of failing one resource.
- **Other DXGI DDIs on foreign threads.** Only `TrimResidencySet` was seen
  on a thread that does not own the device. The other `DXGI_DDI` entry points
  were not audited for the same problem.
