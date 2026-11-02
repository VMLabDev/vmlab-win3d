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
The `VGTD` status also retains the last submitted/completed fence and pending
preemption from the first outstanding engine at reset.

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

This document describes the test contract. A passing forced-TDR result must come
from a guest run; compiling the executable or parsing the scripts does not prove
recovery.
