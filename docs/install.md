# Install

This page builds the vmlab-win3d package and installs it in a Windows 10 or
11 vmlab guest. After the install, Direct3D 9, 10, 11 and 12, OpenGL and
Vulkan applications in the guest render on the host's Vulkan driver through
virtio-gpu Venus, with no per-application files or settings.
`docs/uninstall.md` reverses the install.

## Prerequisites

On the host:

- Docker, usable without sudo, and network access for the pinned downloads.
- vmlab 0.24.0-alpha or later, and the local `windows-11` (or `windows-10`)
  template. Earlier vmlab versions do not give the guest a working Venus
  device; `docs/host.md` records why.
- A host GPU whose Vulkan driver Venus works on: AMD with RADV (the
  reference; on helios the iGPU at PCI address `0000:72:00.0`,
  `/dev/dri/renderD129`) or Intel with ANV. NVIDIA needs a patched QEMU and
  is only partly working, and a laptop RTX 4060 failed outright
  ([known limits](known-limits.md#host-gpu-support)).

The VM needs two settings, as in `labs/probe/vmlab.wcl`:

| Setting | Why |
| --- | --- |
| `secure_boot = false` | The package is test-signed (decision 0001). Windows ignores `testsigning` while Secure Boot is on, so `Install.cmd` refuses to run. This cannot be changed from inside the guest. |
| `gpu { mode = "vulkan" address = "0000:72:00.0" }` | vmlab's Venus mode. vmlab 0.24 emits one `virtio-vga-gl,venus=true,blob=true,hostmem=4G` adapter. `address` names the host GPU: vmlab puts egl-headless on its render node and sets `VK_LOADER_DRIVERS_SELECT` in QEMU's environment, so virglrenderer uses RADV. The KMD needs the device's `VIRTIO_GPU_F_VIRGL` feature; the INF matches its hardware ID, `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01`. |

Find the address of the GPU with `lspci -D | grep -i vga`, or from
`/sys/class/drm/renderD*/device`. `gpu.vulkan_icd = "<path to ICD JSON>"`
overrides the ICD choice when the driver name is not enough.

A lab for the package:

```wcl
import <vmlab.wcl>

lab "win3d-package" {
  vm "win11" {
    template = "x86_64/windows-11@26100.1742.5"
    secure_boot = false
    gpu {
      mode    = "vulkan"
      address = "0000:72:00.0"
    }
  }
}
```

Bring it up with plain `vmlab up`. QEMU's log, `qemu.log` in the VM's state
directory, starts with `vmlab: argv:` and `vmlab: env:` lines, so you can
check the device line and the ICD filter there.

The evidence in `docs/evidence/package/` predates vmlab 0.24. It used vmlab
0.23 with `qemu_args` and an isolated supervisor that had
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json` in its environment
(`docs/evidence/package/lab-vmlab.wcl`, `docs/host.md`). The guest sees the
same device either way: `virtio-vga-gl` and `virtio-gpu-gl` present the same
PCI ID.

## 1. Build

From a clean checkout:

```sh
scripts/build.sh
```

It runs every component build in Docker (`build/toolchain`, `build/mesa`,
`build/vulkan-sdk`, `build/d3d12`, `build/kmd`, `build/probes`), creates the
test signing certificate in `.signing/` on the first run
(`build/signing/make-test-cert.sh`), then assembles and signs the bundle
(`build/package/build.sh`). `scripts/build.sh --package-only` repeats only
the last two steps.

The output is in `out/package/`:

```
vmlab-win3d-0.1.0/            the bundle
vmlab-win3d-0.1.0.zip         the same bundle, zipped
SHA256SUMS
```

Bundle layout:

| Path | Content |
| --- | --- |
| `Install.cmd`, `Uninstall.cmd` | Entry points; they run `vmlab-win3d.ps1` in 64-bit PowerShell |
| `vmlab-win3d.ps1` | The installer (`installer/vmlab-win3d.ps1`) |
| `vmlab-win3d-test.cer` | The public half of this checkout's test certificate |
| `manifest.json` | Version, commit, driver version, hardware ID, certificate thumbprint, and the sha256 of every file. The installer refuses a bundle that does not match it. |
| `driver/` | The test-signed driver package: `viogpu3d.inf`, `viogpu3d.cat`, `viogpu3d.sys`, `vgpusrv.exe`, `viogpuap.exe`, and `x64/`, `x86/` with `viogpu_d3d10_*.dll`, `viogpu_wgl_*.dll`, `libvulkan_virtio_*.dll` and the ICD manifests `virtio_icd.x86_64.json`, `virtio_icd.x86.json` |
| `runtime/x64/`, `runtime/x86/` | `vulkan-1.dll`, `vulkaninfo.exe` (Khronos loader 1.4.363), and the D3D12 bridge `d3d12.dll` with `vkd3dcore.dll` and `dxgi-vulkan.dll` |

`build/package/make-inf.py` writes `viogpu3d.inf` from the KMD build's
`out/kmd/viogpu3d_x64_wow64.inf.template`, and `build/package/check-inf.py`
checks that the package holds exactly the files the INF lists. Each checkout
has its own certificate, so a guest must trust the certificate of the
checkout that built its bundle; `Install.cmd` imports the one in the bundle.

## 2. Install

### From the host

```sh
scripts/install-into-vm.sh <lab-dir> <vm>
```

It copies the bundle to `C:\vmlab-win3d` in the guest (`vmlab cp`), runs
`Install.cmd` as SYSTEM (`vmlab exec`), and waits through the reboots until
`C:\ProgramData\vmlab-win3d\state.json` says `Installed`. It exits 0 on
success, 1 on failure (and prints the end of the journal), and 124 on a
timeout (`INSTALL_TIMEOUT`, default 2400 s). `--bundle DIR` installs another
bundle; `VMLAB=<command>` runs another vmlab binary.

### In the guest

Copy the bundle (or unzip it) anywhere, then from an **elevated** command
prompt:

```bat
Install.cmd
```

Windows restarts by itself, once or twice, and setup continues after each
restart. Do not log off or shut down until `state.json` says `Installed`:

```bat
powershell -ExecutionPolicy Bypass -File C:\ProgramData\vmlab-win3d\vmlab-win3d.ps1 -Mode Status
```

### What happens

| Step | Changes | Reboot |
| --- | --- | --- |
| Preflight | Refuses if not elevated, if `Confirm-SecureBootUEFI` says Secure Boot is on, if no display device has the INF's hardware ID, or if a bundle file does not match `manifest.json`. Changes nothing. | — |
| Stage | Copies the bundle to `C:\ProgramData\vmlab-win3d\bundle\` and the script to `C:\ProgramData\vmlab-win3d\`. | — |
| Baseline (first install only) | Writes `baseline.json`: testsigning, hibernation and Fast Startup, whether the certificate is trusted, the display device's driver INF, version and problem code, the hash, version and security descriptor of every system file the package touches, and the Vulkan registry values. Copies the original `d3d12.dll`, `vulkan-1.dll` and `vulkaninfo.exe` (System32 and SysWOW64) to `backup\`. | — |
| Trust and boot policy | `certutil -f -addstore Root` and `TrustedPublisher` with `vmlab-win3d-test.cer`; `powercfg /hibernate off` (which also turns off Fast Startup); `bcdedit /set {current} testsigning on`. | If test signing is not active in the running kernel, registers the startup task `vmlab-win3d-resume` and restarts. |
| Driver | Removes older builds of this package from the driver store (`pnputil /delete-driver oemNN.inf /uninstall /force`), then `pnputil /add-driver viogpu3d.inf /install`. Checks that the device now uses this package through `DEVPKEY_Device_DriverInfPath`. The INF copies the UMDs and the Venus ICD into System32 and SysWOW64 and registers them (below). | — |
| Vulkan loader | Installs `vulkan-1.dll` and `vulkaninfo.exe` into System32 (x64) and SysWOW64 (x86) when the existing `vulkan-1.dll` is absent or has an older file version (the binary version resource is compared). Otherwise it keeps the existing loader and installs `vulkaninfo.exe` only where none exists. The template's loader is 1.3.300; the package's is 1.4.363. | — |
| D3D12 bridge | For System32 and SysWOW64: copies the machine's own `d3d12.dll` to `vmlab-win3d\d3d12-native.dll`, copies `vkd3dcore.dll` and `dxgi-vulkan.dll` there, takes ownership of `d3d12.dll` (`takeown /A`, `icacls /grant`), replaces it with the bridge, and puts back the original owner (TrustedInstaller) and ACL. | — |
| Repair task | Registers `vmlab-win3d-repair`, a SYSTEM startup task. | — |
| Final reboot | Registers `vmlab-win3d-resume` and restarts, unless nothing changed. | Yes |
| Verify | After the restart: the device uses this package with problem code 0, `UserModeDriverName` and `OpenGLDriverName` are set, both bridges and `d3d12-native.dll` are in place, and both Vulkan ICDs are registered. Sets `state.json` to `Installed` and removes the resume task. | — |

A first install from the template takes two restarts. Once test signing is
active, an upgrade takes one, and a rerun of the same bundle takes none.

### Registration

From the INF, in the display adapter's class key
(`HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-…}\<n>`):

| Value | Data |
| --- | --- |
| `UserModeDriverName` | `C:\Windows\System32\viogpu_d3d10.dll` ×3 (D3D9, D3D10, D3D11) |
| `UserModeDriverNameWow` | `C:\Windows\SysWOW64\viogpu_d3d10.dll` ×3 |
| `InstalledDisplayDrivers` | `viogpu_d3d10` ×3 |
| `OpenGLDriverName`, `OpenGLDriverNameWow` | `viogpu_wgl.dll` in System32, SysWOW64 |
| `OpenGLFlags`, `OpenGLFlagsWow` | 3 |
| `OpenGLVersion`, `OpenGLVersionWow` | 4096 |

and, machine-wide:

| Key | Value | Data |
| --- | --- | --- |
| `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` | `C:\Windows\System32\virtio_icd.x86_64.json` | DWORD 0 |
| `HKLM\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers` | `C:\Windows\SysWOW64\virtio_icd.x86.json` | DWORD 0 |

Each ICD manifest sits next to its `libvulkan_virtio.dll`. There is no D3D12
UMD entry: D3D12 goes through the bridge. Unlike Yttrium's release INF, the
package does not overwrite `opengl32.dll`, and it sets no `TdrDebugMode` or
`EnablePreemption`.

### State and logs

`C:\ProgramData\vmlab-win3d\`:

| File | Content |
| --- | --- |
| `state.json` | `status` (`Installing`, `RebootPending`, `Installed`, `Uninstalling`, `Removed`, `Failed`), `step`, `last_error`, version, the bound INF |
| `journal.log` | Every action and every command's output, timestamped |
| `baseline.json` | The machine before the first install; uninstall restores it |
| `backup\` | Original `d3d12.dll`, `vulkan-1.dll` and `vulkaninfo.exe` |
| `bundle\` | The staged bundle, which the startup tasks run from |

### Rerun and upgrade

`Install.cmd` is idempotent. Each step checks before it changes anything:
the certificate and boot settings are left alone when already set, the
driver package is replaced only when its catalog differs from the bundle's,
and system files are copied only when their hash differs. Running a newer
bundle's `Install.cmd` upgrades in place and keeps the original baseline.

### Windows servicing

A cumulative update can put Microsoft's `d3d12.dll` back. At every boot,
`vmlab-win3d-repair` checks both `d3d12.dll` files. A file that is not the
bridge is treated as the new native DLL: it becomes `d3d12-native.dll` and
the newest backup, and the bridge goes back in place. The journal records it.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `Secure Boot is on` | The lab needs `secure_boot = false`. |
| `No display device with hardware ID …` | The VM has no virtio-gpu GL device: no `gpu { mode = "vulkan" }` (or `virgl`). |
| Device problem code 52 | The signature was rejected: test signing is not active, or the certificate is not trusted. Check `bcdedit /enum {current}`. |
| Device problem code 43 | The KMD started but failed, for example without Venus or blob on the host side. |
| `The display device uses oemNN.inf …, not this package's` | A better-ranked driver for the same hardware ID is in the driver store. Remove it with the `pnputil` command in the message and run `Install.cmd` again. |
| Vulkan lists no device from `vmlab exec` | Vulkan does not enumerate from the SYSTEM session. Run tests in a user session (`tests/guest/Run-Probe.ps1`). |

## Evidence

`docs/evidence/package/README.md`: a fresh-clone build, then install,
probes, rerun, uninstall and reinstall on a fresh `windows-11` lab.
