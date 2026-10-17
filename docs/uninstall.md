# Uninstall

This page removes the vmlab-win3d package from a Windows guest and returns
the guest to the state it was in before the first install. `docs/install.md`
covers the install.

## Prerequisites

- The package was installed with `Install.cmd`, so
  `C:\ProgramData\vmlab-win3d\baseline.json` and `bundle\` exist.
- No install or uninstall is waiting for a reboot (`state.json` says
  `Installed` or `Failed`).
- An elevated prompt, or SYSTEM through `vmlab exec`.

## Run it

From the host:

```sh
scripts/install-into-vm.sh --uninstall <lab-dir> <vm>
```

It runs `C:\ProgramData\vmlab-win3d\bundle\Uninstall.cmd` as SYSTEM and waits
until `state.json` says `Removed`. Exit status: 0 removed, 1 failed (the end
of the journal is printed), 124 timed out.

In the guest, from an elevated command prompt:

```bat
C:\ProgramData\vmlab-win3d\bundle\Uninstall.cmd
```

Windows restarts once, and a startup task checks the result after the
restart. Uninstall on a machine where nothing is installed changes nothing.

## What it changes

Uninstall restores the values recorded in `baseline.json` at the first
install. It changes only what the package changed, and leaves alone anything
that something else changed since.

| Step | Changes |
| --- | --- |
| Repair task | Removes `vmlab-win3d-repair`. |
| D3D12 bridge | For System32 and SysWOW64: if `d3d12.dll` is still the bridge, takes ownership, moves it aside and copies back the native DLL: `vmlab-win3d\d3d12-native.dll`, which is the newest native file (the repair task refreshes it when Windows servicing replaces `d3d12.dll`), or the backup. Restores the owner (TrustedInstaller) and the ACL. Removes `System32\vmlab-win3d\` and `SysWOW64\vmlab-win3d\`. |
| Vulkan loader | For `vulkan-1.dll` and `vulkaninfo.exe` in System32 and SysWOW64: if the file is still the one the package installed, restores the backup with its owner and ACL, or deletes the file when the machine had none. |
| Driver | `pnputil /delete-driver oemNN.inf /uninstall` for every viogpu3d package in the driver store. PnP then puts the best remaining driver on the device, which on the vmlab templates is Red Hat's `viogpudo.inf` (VirtIO GPU DOD). If the device does not return to the baseline driver, runs `pnputil /scan-devices`, then `pnputil /add-driver <baseline INF> /install`. |
| Display registry | PnP keeps the device's class key (`Control\Class\{4d36e968-…}\<n>`) for the next driver, so the INF's `UserModeDriverName[Wow]`, `OpenGLDriverName[Wow]`, `OpenGLFlags[Wow]`, `OpenGLVersion[Wow]`, `InstalledDisplayDrivers`, `HWCursor`, `FlexResolution` and `UsePhysicalMemory` survive a driver change. Each is put back to its baseline value, or deleted when the baseline had none. The same applies to `MSISupported` and `MessageNumberLimit` in the device's `Device Parameters\Interrupt Management\MessageSignaledInterruptProperties`. |
| Services | Deletes the `VioGpu3D` and `vgpusrv` services when the baseline had none. |
| INF files | Deletes `viogpu_d3d10.dll`, `viogpu_wgl.dll`, `libvulkan_virtio.dll` and the `virtio_icd.*.json` manifests from System32 and SysWOW64 when the baseline had none, and the two `Khronos\Vulkan\Drivers` values. |
| Certificate | `certutil -delstore Root` and `TrustedPublisher` for the test certificate, unless it was trusted before the install. |
| Boot policy | `bcdedit /deletevalue {current} testsigning` when the baseline had no entry (the vmlab templates have none), otherwise sets it back to the recorded value. `powercfg /hibernate on` if hibernation was on, and `HiberbootEnabled` (Fast Startup) back to its value. |
| Reboot | Registers `vmlab-win3d-resume` and restarts. |
| Verify | After the restart: the device uses the baseline INF with the baseline problem code, every file the package touched has its baseline hash (or is absent again), the registry values, testsigning, certificates and hibernation match the baseline, no viogpu3d package is left in the driver store, and the private directories are gone. Then removes the resume task. |

After the restart, `C:\ProgramData\vmlab-win3d\` keeps only `state.json`
(`Removed`), `journal.log`, the script, and `history\baseline-<time>.json`.
`backup\` and `bundle\` are deleted. Delete the directory by hand if you want
no trace. Running `Install.cmd` again later records a new baseline.

## When it fails

`state.json` then says `Failed` with `last_error`, and `journal.log` lists
every step and command output. The verification step names each difference
from the baseline, for example a file whose hash differs because Windows
Update replaced it after the install. Those are reported, not reverted.

Uninstall does not undo changes the package did not make, such as the test
user and autologon that `tests/guest/Set-TestDesktop.ps1` creates.
