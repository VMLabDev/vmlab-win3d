# Guest test scripts

PowerShell 5.1 scripts that run inside a Windows guest and collect the
evidence for the Done criteria in `docs/brief.md`. Copy this directory and
`out/probes/` into the guest, for example to `C:\win3d\guest` and
`C:\win3d\probes`, and run the scripts elevated (`vmlab exec` runs as SYSTEM
when the lab declares no `login {}`).

| Script | What it does | Output |
| --- | --- | --- |
| `Set-TestDesktop.ps1` | Waits for Windows' user OOBE to finish (it clears autologon when it exits), then creates the local user `win3d` and turns on Winlogon autologon for it. Also stops the display from blanking and disables the lock screen. Run it once with `-Reboot`. | The user's desktop in session 1 after the reboot |
| `Run-Probe.ps1` | Runs one program in the logged-on user's session through a one-off scheduled task with an interactive logon. While it runs, the script samples the loaded modules with Toolhelp, WOW64 included. | `<Name>.json` (the probe's result), `<Name>.modules.json`, `<Name>.run.json`; prints the run summary |
| `Run-AllProbes.ps1` | Runs every `x64` and `x86` probe through `Run-Probe.ps1`. With `-Warp`, it also runs D3D11 and D3D12 a second time with `--warp`. | All of the above, plus `summary.json` and a table |
| `Get-Dxdiag.ps1` | Runs `dxdiag /whql:off /t`, waits for the report, and extracts the display device fields. Optional switches: `-Xml` also writes the XML report, `-AsUser` runs dxdiag in the user's session, and `-Parse <file>` parses an existing report. | `dxdiag.txt`, `dxdiag.json` |
| `Get-GpuState.ps1` | Collects display device PnP status and problem code, and the driver INF, version and provider. Also reads the class-key registry (`UserModeDriverName*`, `OpenGLDriverName*`, …), the Khronos Vulkan driver and layer keys in both registry views, the versions and hashes of the system graphics DLLs, and the testsigning, Secure Boot and Fast Startup state. | JSON on stdout and in `-OutFile` |
| `Test-GuestScripts.ps1` | Parses every script here with the running PowerShell. | `ok` per file; exits nonzero on a syntax error |

## Typical run

```powershell
# once, as SYSTEM or an administrator:
C:\win3d\guest\Set-TestDesktop.ps1 -Reboot
# after the reboot, once the desktop is up:
C:\win3d\guest\Run-AllProbes.ps1 -ProbeDir C:\win3d\probes -OutDir C:\win3d\results -Seconds 15 -Warp
C:\win3d\guest\Get-Dxdiag.ps1 -OutDir C:\win3d\results\dxdiag -AsUser
C:\win3d\guest\Get-GpuState.ps1 -OutFile C:\win3d\results\gpu-state.json
```

From the host:

```sh
vmlab exec --timeout 1400 w11 -- powershell -NoProfile -ExecutionPolicy Bypass \
  -File C:/win3d/guest/Run-AllProbes.ps1 -Seconds 15 -Warp
```

## Things these scripts take care of

- **Session 1.** Vulkan does not enumerate devices from SYSTEM, and only the
  user's session is on the console. `Run-Probe.ps1` targets the console user,
  `Win32_ComputerSystem.UserName`, or the owner of `explorer.exe`, and fails
  when nobody is logged on.
- **Queued tasks.** An interactive task stays `Queued` until the user's logon
  completes. On a fresh vmlab Windows 11 clone, the first logon shows
  "Preparing Windows" until about 5 minutes of uptime. `Run-Probe.ps1` waits
  up to 180 s for the process to appear, and its default timeout is
  `Seconds + 300`.
- **Exit code.** It comes from a handle opened on the process while it runs,
  with the task's `LastTaskResult` as a fallback. `Run-Probe.ps1` exits with
  it, or with 124 on a timeout and 125 when nothing started.
- **Module provenance.** Each module is tagged `system32`, `syswow64`,
  `system_subdir`, `driverstore`, `app_local` (same directory as the exe) or
  `other`. `graphics_modules` in the run summary lists only the
  graphics-related ones, and `app_local_modules` must be empty for an
  "unmodified application" test.
- **dxdiag wording.** Current dxdiag text reports say `DDraw Status`,
  `D3D Status` and `AGP Status`; older reports and the UI say "… Acceleration".
  Both are read. dxdiag reports `Enabled` even for a display-only driver whose
  3D runs on the Microsoft Basic Render Driver (WARP). The summary's
  `hardware_acceleration` is therefore true only when the device is neither
  display-only nor vendor 0x1414 device 0x008C.
- **Registry and PnP.** Driver facts come from `DEVPKEY_*` properties, which
  update at once after `pnputil`, unlike `Win32_PnPSignedDriver`
  (prior-art §8.3).
