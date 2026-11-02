# Test TDR recovery

Run this test only in a disposable Windows VM. It simulates a Windows guest
timeout on PCI virtio-gpu `1af4:1050`; it does not deliberately hang the physical
host GPU. The KMD must implement the read-only `VGTD` diagnostic escape.

Build the console probe with the existing cross toolchain:

```sh
build/probes/build.sh tdr
```

Copy `out/probes-extra/x64/tdr_probe.exe`, an existing hardware rendering probe,
`tests/guest/Run-Probe.ps1`, and `tests/guest/Test-TdrRecovery.ps1` to the guest.
Run elevated after a console user has logged on:

```powershell
C:\win3d\guest\Test-TdrRecovery.ps1 `
    -TdrProbe C:\win3d\tdr_probe.exe `
    -RenderProbe C:\win3d\probes\x64\vulkan_probe.exe `
    -OutDir C:\win3d\tdr-results
```

The native probe uses `D3DKMT_ESCAPE_TDRDBGCTRL` with the four-byte
`D3DKMT_TDRDBGCTRLTYPE_FORCETDR` value. Microsoft documents that this escape
requires `HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\TdrTestMode`
to be DWORD 1. The probe preserves the previous DWORD or value absence and
restores it after the request and recovery observation. It does not change
`TdrDelay`, `TdrDebugMode`, or other timeout policy. If Windows bugchecks before
the probe returns, restoration cannot run; discard that test clone.
Source: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_escape.

Exit 0 requires all of the following evidence in `tdr-recovery.json`:

- E1: The forced request returned success and the temporary registry value was restored.
- E2: KMD reset and restart counters both increased, both final statuses are zero, and recovery state is idle.
- E3: Hardware rendering passed pixel readback before and after the timeout.
- E4: Windows boot time is unchanged and no new System BugCheck or Application BlueScreen event records appeared.

An accepted escape alone is insufficient. Missing telemetry, a counter that did
not advance, failed rendering, changed boot time, a new crash record, or a failed
registry restoration makes the harness fail. Existing crash history is retained;
the host regression additionally rejects preexisting BlueScreen reports so an
OOBE crash before the guest agent first answers cannot be hidden by a later pass.

The optional host regression input `VMLAB_HV_TEST_WIN3D_TDR_PROBE` points at the
compiled executable. Its guest invocation is `--force-tdr --out FILE` as SYSTEM.
The console request itself has no interactive-session requirement; the rendering
checks use `Run-Probe.ps1` in the logged-on user's desktop. The complete CLI,
JSON fields, and exit codes are documented in `probes/tdr/README.md`.

Use `tdr_probe.exe --trace --out FILE` before another forced timeout to retain
the last stall's trace. `VGTX` returns a bounded ring with generation, context,
fence, response, ISR status, and preemption fields. A timeout freezes its
pre-reset ring; ordinary traffic after restart cannot overwrite that snapshot.
Event values are 1 submit, 2 completion, 3 preempt, 4 ISR status, 5 response,
6 reset start, 7 reset completion, 8 restart completion, and 9 power transition.
The `VGTD` status retains the last submitted/completed fence and pending
preemption from the first outstanding engine at reset. If every engine has
drained, it retains the render engine's matching fence values. Before the first
timeout it reports live render-engine values. These are OS DMA fences, distinct
from Vulkan's user-mode synchronization and unfenced control requests.

## Backend prerequisite

The KMD waits for virtio device status zero before releasing command payloads
or invalidating mappings. The host must stop memory access before acknowledging
reset. Microsoft states the reset contract at
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetfromtimeout.
The virtio reset contract is at
https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html.

In vmlab-hv, renderer teardown errors are logged but do not prevent reset
acknowledgement. The current Venus proxy sends destruction messages without
waiting for worker exit or GPU idle. Removal of guest BAR aliases limits the
guest-memory lifetime risk, but an explicit backend quiescence acknowledgement
is still missing. Record that limitation separately from functional recovery.

## Feedback mapping regression

The initial recovery candidate exposed an Explorer access violation in
`viogpu_d3d10.dll` at RVA `0x24b56d`. Matching packaged and unstripped code
identifies an unguarded batch-feedback read in
`yttrium_venus_wait_batch_feedback`. A reset can revoke the cached CPU mapping
after the separate ring check succeeds.

Mesa patch 0093 uses checked ring accesses for feedback reads and writes.
Mapping failure marks the backend failed, invokes its existing reset callback,
and returns `VK_ERROR_DEVICE_LOST` or a rejected store. A host-fence success
cannot override feedback device loss. x64 uses compiler SEH. The pinned
GNU-target x86 compiler cannot emit its SEH filters, so x86 registers a
thread-scoped exception frame around the direct memory-access leaves. Its
handler accepts access/in-page faults during normal dispatch. A C setjmp
boundary returns failure and restores the prior exception chain.
Unrelated and unwind exceptions continue searching. No C++ objects are
abandoned at this boundary.

The x86 object emits a SafeSEH annotation, but the current MinGW DLL load
configuration has no SafeSEH table. The native checks exercise Windows'
default exception dispatch; they do not establish SafeSEH table enforcement.

An intermediate checked-copy candidate passed private-page fault tests but
failed native x86 D3D11 startup on both NVIDIA and AMD with
`DXGI_ERROR_DEVICE_REMOVED`, before forced TDR. It is not an accepted package.
The direct-access replacement must also pass live x64/x86 D3D11 readback
before and after TDR; private-page checks alone do not establish GPU-aperture
access.

`probes/feedback-loss/build.py` links the actual production feedback, ring,
and reset-callback code using the materialized Mesa build's compiler and link
commands. `tests/guest/Test-FeedbackLoss.ps1` runs its two architectures in
Windows. Red requires two healthy controls and eight access-violation exits
after feedback pages become inaccessible or unmapped. Green requires ten
successful process exits; the eight mapping-loss cases must report device loss,
backend failure, and exactly one reset callback. Both phases passed on Windows
PowerShell `5.1.26100.9711`. These focused checks do not prove GPU quiescence.

## Accepted native verification

Package `0.1.3`, KMD `100.6.101.58104`, passed four fresh native first boots
from template `26100.1742.10#win3d`, twice on NVIDIA and twice on AMD. All 24
Vulkan and x64/x86 D3D11 rendering results passed exact RGB readback before
and after forced TDR. Each run advanced both recovery counters from zero to
one, returned successful reset/restart statuses, restored TdrTestMode, and
continued without a bugcheck or OS reboot. Frozen fence snapshots were
`5589/5589`, `5580/5580`, `5534/5534`, and `5734/5734`; pending preemption
was zero. These drained snapshots do not identify the original unforced stall.

A OneDrive access violation in `ucrtbase.dll`, exception `c0000005` at offset
`ee3e7`, appeared after forced TDR in three runs. The owner explicitly excluded
OneDrive from this task and stopped that investigation. It was deliberately
not pursued and does not block acceptance. The acceptance bar is recovered
Vulkan plus x64/x86 D3D11 readback with no bugcheck. Existing WER evidence is
retained; no claim is made about the OneDrive fault's cause.

This document describes the test contract. A passing forced-TDR result must come
from a guest run; compiling the executable or parsing the scripts does not prove
recovery.
