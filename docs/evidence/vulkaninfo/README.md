# Evidence: patched vulkaninfo on Venus

This directory shows that the package's `vulkaninfo.exe` with
`patches/vulkan-tools/0001-vulkaninfo-Load-user32-on-every-Windows-compiler.patch`
runs to completion as the interactive user, for both x64 and x86. It lists
the Venus device and exits 0. Without the patch, both builds crashed with
0xC0000005 at RVA 0x1a97 (x64) and 0x19a9 (x86). Those crashes are recorded in
`docs/evidence/acceptance/windows-11/vulkaninfo/` on branch `work/e2e`.

## Setup

- Lab `win3d-fix-vulkaninfo-1`, from `labs/probe/vmlab.wcl` (only the lab
  name changed). vmlab 0.24.0-alpha, template
  `x86_64/windows-11@26100.1742.5`, `gpu { mode = "vulkan" address =
  "0000:72:00.0" }` (AMD iGPU, RADV), Secure Boot off. 2026-10-17.
- Bundle built from this branch with `scripts/build.sh`, then installed with
  `scripts/install-into-vm.sh .scratch/lab win11`, which ended `Installed`.
- `tests/guest/Set-TestDesktop.ps1 -Reboot` was run to log on the user
  `win3d`. It ran once before the install, and again after it because the
  Winlogon autologon values were gone after the install's restarts.
- `Invoke-Vulkaninfo.ps1` (this directory) ran through `vmlab exec`. It runs
  `System32\vulkaninfo.exe` and `SysWOW64\vulkaninfo.exe`, with `--summary`
  and with no arguments, in session 1 through `tests/guest/Run-Probe.ps1`.

## Results

| File | Binary | Mode | Result |
| --- | --- | --- | --- |
| `vulkaninfo-x64-summary.txt` | System32 (x64) | `--summary` | `exit 0`, `deviceName = Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO))`, `driverID = DRIVER_ID_MESA_VENUS` |
| `vulkaninfo-x64-full.txt` | System32 (x64) | full | `exit 0`, 1,681 lines, with a `VK_KHR_win32_surface` section in "Presentable Surfaces" |
| `vulkaninfo-x86-summary.txt` | SysWOW64 (x86) | `--summary` | `exit 0`, same device |
| `vulkaninfo-x86-full.txt` | SysWOW64 (x86) | full | `exit 0`, 1,682 lines, with the same surface section |

In the full output, the "Presentable Surfaces" section comes from
`AppCreateWin32Window`, the function that crashed before the patch. No
Application Error (event 1000) for `vulkaninfo.exe` was logged during the
run. `vulkaninfo.run.json` is the `Run-Probe.ps1` record: user
`VMLABVM-FUOIQK8\win3d`, session 1, exit code 0.

`files.txt` shows the binaries that ran. Their hashes match the ones in
`build/vulkan-sdk/README.md`:

```
68266ef104567081f93d00cc347ac3c1849cd3d0e1319f8dc05a576b2e23e83a  1.4.363.0  C:\WINDOWS\System32\vulkaninfo.exe
0871d16d06f2c7c2f60c9b68e5ed3a61a069bf525d8f7cad2d3c88f3ce71e219  1.4.363.0  C:\WINDOWS\System32\vulkan-1.dll
bb717db3466b5120b08efb4a800ad7d4f15cc1c0d5d479c298804539b89c119d  1.4.363.0  C:\WINDOWS\SysWOW64\vulkaninfo.exe
36edc9f67cbec1c5fb8c510ab08efea18afffbd40d77b829d39f1e76236416cd  1.4.363.0  C:\WINDOWS\SysWOW64\vulkan-1.dll
```

Each output begins with loader warnings. "Registry lookup failed to get
layer manifest files" means no implicit layers are registered. The x86 runs
also say "Path to given binary C:\WINDOWS\SysWOW64\libvulkan_virtio.dll was
found to differ from OS loaded path C:\WINDOWS\System32\libvulkan_virtio.dll",
a loader notice about the SysWOW64 and System32 paths. These warnings come from
the loader and the ICD registration, not from vulkaninfo, and this patch does
not touch them.

The lab was destroyed after the run.
