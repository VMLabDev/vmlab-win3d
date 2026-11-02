# Forced guest TDR probe

`tdr_probe.exe` asks Windows to simulate a timeout on the guest's virtio-gpu
adapter. It does not submit a hanging shader or deliberately hang the physical
host GPU. It accepts only one PCI adapter with vendor `1af4` and device `1050`.
Run it elevated in a disposable VM.

Build explicitly, outside the rendering probes that `Run-AllProbes.ps1` discovers:

```sh
build/probes/build.sh tdr
```

The executables are `out/probes-extra/x64/tdr_probe.exe` and
`out/probes-extra/x86/tdr_probe.exe`. They are console programs with only Windows
system DLL imports.

## Interface

```text
tdr_probe.exe --status [--out FILE]
tdr_probe.exe --trace [--out FILE]
tdr_probe.exe --force-tdr [--out FILE] [--wait-ms 100..120000]
```

`--status` reads KMD diagnostic counters. `--force-tdr` snapshots the counters,
temporarily sets `GraphicsDrivers\TdrTestMode` to DWORD 1, calls the OS
`D3DKMT_ESCAPE_TDRDBGCTRL` escape with `D3DKMT_TDRDBGCTRLTYPE_FORCETDR`, and waits
up to 30 seconds for successful reset and restart telemetry. It restores the
previous DWORD or value absence before returning. A guest bugcheck can prevent
that restoration; discard the test clone in that case. No other TDR registry
values are changed.

The program writes one JSON object to stdout and to `--out`, when supplied.
Before the forced call, it flushes a `result="requesting"` record to the output
file so a bugcheck can leave evidence of the attempted request. Completion is
`result="request_accepted"`, `request_completed=true`, the actual hexadecimal
`escape_status`, and `recovery_observed`. Counter fields are `reset_before`,
`reset_after`, `restart_before`, and `restart_after`; unavailable counters are
JSON null. `last_reset_status`, `last_restart_status`, and `recovery_state`
describe the final KMD snapshot. `test_mode_restore_status` is the Win32 result
of restoring the registry value. Exit 0 requires successful recovery telemetry
and registry restoration. Exit 1 is an API, adapter-selection, or output error;
exit 2 means the request was accepted but recovery was not proved; exit 3 is
invalid command-line usage. An old driver without telemetry is still sent the
forced request, so its failed recovery can be tested.

The read-only `VGTD` driver escape uses magic `0x56475444`, version 1, and a
72-byte payload. It exposes reset and restart counts, their last statuses,
submitted and completed fences, pending preemption, and recovery state. Success
requires both counters to increase, both statuses to be zero, and recovery state
to return to zero.

`--trace` reads the separate `VGTX` escape (`0x56475458`, version 1): a 24-byte
header and 256 56-byte records, totaling 14360 bytes. It does not request a TDR
or change the registry. JSON includes the actual `trace_status`, `trace_count`,
`trace_last_sequence`, and `trace_records`; only records with a nonzero sequence
are emitted. Each record contains `sequence`, `tick`, `generation`, `event`,
`node`, `engine`, `context`, `flags`, `fence`, `response`, and
`pending_preemption`. Ring storage order is retained; use `sequence` to order
events. A failed escape returns exit 1 and null trace data.

Trace event numbers are 1 submit, 2 GPU completion, 3 preempt request,
4 ISR status, 5 response, 6 reset start, 7 reset done, 8 restart done, and
9 power transition. The bounded ring supplies diagnostic evidence; absence of
an older event does not establish that it never occurred.

## Recovery and rendering check

Copy `tests/guest/Run-Probe.ps1`, `tests/guest/Test-TdrRecovery.ps1`, the TDR
executable, and an existing rendering probe into the VM. After a console user
has logged on, run:

```powershell
Test-TdrRecovery.ps1 -TdrProbe C:\win3d\tdr_probe.exe `
    -RenderProbe C:\win3d\probes\x64\vulkan_probe.exe `
    -OutDir C:\win3d\tdr-results
```

The harness requires hardware pixel readback before and after TDR, positive
reset and restart telemetry, an unchanged Windows boot time, and no new System
BugCheck or Application BlueScreen event records. Results and event evidence
are saved in `tdr-recovery.json` and the normal render-probe result files.

## Microsoft API references

The documented test prerequisite and four-byte control value are described at
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_escape.
The forced-TDR control is described at
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ne-d3dkmthk-_d3dkmt_tdrdbgctrltype.
The adapter enumeration and handle lifetime are described at
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtenumadapters2.
