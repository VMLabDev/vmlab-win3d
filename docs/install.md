# Install

This page builds the vmlab-win3d package and installs it in a Windows 10 or
11 vmlab guest. After the install, Direct3D 9, 10, 11 and 12, OpenGL and
Vulkan applications in the guest render on the host's Vulkan driver through
virtio-gpu Venus, with no per-application files or settings.
`docs/uninstall.md` reverses the install.

## Prerequisites

On the host:

- Docker, usable without sudo, and network access for the pinned downloads.
- vmlab 0.27.0-alpha or later, and the local `windows-11` (or `windows-10`)
  template. vmlab before 0.24 does not give the guest a working Venus
  device (`docs/host.md` records why), 0.24 lab files name the GPU by PCI
  address, which 0.25 rejects, 0.26 added the host GPU's `qemu_binary` that
  NVIDIA needs, and 0.27 hides the host's other GPUs from the guest.
- A host GPU whose Vulkan driver Venus works on: AMD with RADV (the
  reference; on helios the iGPU at PCI address `0000:72:00.0`,
  `/dev/dri/renderD129`), Intel with ANV, or an NVIDIA RTX 5090. A laptop
  RTX 4060 failed outright
  ([known limits](known-limits.md#host-gpu-support)).
- **For an NVIDIA GPU, a patched QEMU named as `qemu_binary` on that GPU's
  host config block.** Stock QEMU 11.1.1 shows a black console for a Venus
  guest on NVIDIA. `docs/nvidia.md`, "Host setup", says how to build it
  (`patches/host/build-qemu.sh`) and where to point.
- That GPU declared by name in the host config,
  `~/.config/vmlab/config.wcl`. Find its address with
  `lspci -D | grep -Ei 'vga|3d|display'`, or from
  `/sys/class/drm/renderD*/device`. On helios:

  ```wcl
  import <vmlab-host.wcl>

  gpu "AMD1"    { address = "0000:72:00.0" }                  // AMD iGPU (RADV, renderD129)
  gpu "Nvidia1" {                                             // RTX 5090
    address     = "0000:01:00.0"
    default     = true
    qemu_binary = "/home/wil/.local/opt/qemu-11.1.1-vmlab-egl/bin/qemu-system-x86_64"
  }
  gpu "Nvidia2" {                                             // RTX 5090
    address     = "0000:05:00.0"
    qemu_binary = "/home/wil/.local/opt/qemu-11.1.1-vmlab-egl/bin/qemu-system-x86_64"
  }
  ```

  One GPU is the default: a lone declared GPU, or the one with
  `default = true`. A lab that does not name a GPU gets the default. On
  helios that is `Nvidia1`, an RTX 5090; the labs here name their GPU
  explicitly.

The VM needs two settings, as in `labs/probe/vmlab.wcl`:

| Setting | Why |
| --- | --- |
| `secure_boot = false` | The package is test-signed (decision 0001). Windows ignores `testsigning` while Secure Boot is on, so `Install.cmd` refuses to run. This cannot be changed from inside the guest. |
| `gpu { mode = "vulkan" host_gpu = "AMD1" }` | vmlab's Venus mode. vmlab emits one `virtio-vga-gl,venus=true,blob=true,hostmem=4G` adapter. `host_gpu` names a host GPU from the host config: vmlab puts egl-headless on its render node and sets `VK_LOADER_DRIVERS_SELECT` in QEMU's environment from its kernel driver, so virglrenderer uses that GPU's driver (RADV for `AMD1`). vmlab 0.27 also loads a Vulkan layer that hides every other host GPU, so the guest sees one Venus device. The KMD needs the device's `VIRTIO_GPU_F_VIRGL` feature; the INF matches its hardware ID, `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01`. |

`vulkan_icd = "<path to ICD JSON>"` on the host config's `gpu "<name>" {}`
block overrides the ICD choice when the driver name is not enough. A
`host_gpu` the host does not declare makes `vmlab validate` warn and fall
back to the default GPU.

A lab for the package:

```wcl
import <vmlab.wcl>

lab "win3d-package" {
  vm "win11" {
    template = "x86_64/windows-11@26100.1742.5"
    secure_boot = false
    gpu {
      mode     = "vulkan"
      host_gpu = "AMD1"
    }
  }
}
```

Bring it up with plain `vmlab up`. QEMU's log, `qemu.log` in the VM's state
directory, starts with `vmlab: argv:` and `vmlab: env:` lines, so you can
check the device line, the ICD filter and, for a GPU with `qemu_binary`,
the QEMU binary there. On vmlab 0.27 a later
`vmlab-pci-filter: <address>: kept <device>, hid <n>` line names the one
host device the guest sees. `vmlab status -v` shows `host_gpu=<name>`, and
`qemu=<path>` when the GPU has a `qemu_binary`.

The evidence in `docs/evidence/package/` predates vmlab 0.24 and 0.25. It used vmlab
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
| Driver | Lifts this device's pin (below), removes older builds of this package from the driver store (`pnputil /delete-driver oemNN.inf /uninstall /force`), then runs `pnputil /add-driver viogpu3d.inf /install`. If the device still uses another driver (`DEVPKEY_Device_DriverInfPath`), installs this package on it with `UpdateDriverForPlugAndPlayDevices` and `INSTALLFLAG_FORCE`. The INF copies the UMDs and the Venus ICD into System32 and SysWOW64 and registers them (below). | — |
| Pin | Lists the device instance under the device installation policy `DenyInstanceIDs` (`HKLM\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions`), so neither Windows Update nor `pnputil` can put another driver, such as a newer `viogpudo`, on it ([installer.md](installer.md)). | — |
| Vulkan loader | Installs `vulkan-1.dll` and `vulkaninfo.exe` into System32 (x64) and SysWOW64 (x86) when the existing `vulkan-1.dll` is absent or has an older file version (the binary version resource is compared). Otherwise it keeps the existing loader and installs `vulkaninfo.exe` only where none exists. The template's loader is 1.3.300; the package's is 1.4.363. | — |
| D3D12 bridge | For System32 and SysWOW64: copies the machine's own `d3d12.dll` to `vmlab-win3d\d3d12-native.dll`, copies `vkd3dcore.dll` and `dxgi-vulkan.dll` there, takes ownership of `d3d12.dll` (`takeown /A`, `icacls /grant`), replaces it with the bridge, and puts back the original owner (TrustedInstaller) and ACL. | — |
| Repair task | Registers `vmlab-win3d-repair`, a SYSTEM startup task. At each boot it also puts the device back on this package, and the pin back, if either went. | — |
| Final reboot | Registers `vmlab-win3d-resume` and restarts, unless nothing changed. | Yes |
| Verify | After the restart: the device uses this package with problem code 0 and is pinned, `UserModeDriverName` and `OpenGLDriverName` are set, both bridges and `d3d12-native.dll` are in place, and both Vulkan ICDs are registered. Sets `state.json` to `Installed` and removes the resume task. | — |

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
| `baseline.json` | The machine before the first install, including the device installation policy; uninstall restores it |
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
| `The display device still uses oemNN.inf … after a forced install` | Another driver held the device even against `INSTALLFLAG_FORCE`. The journal has the ranked driver list (`pnputil /enum-devices /drivers`). Remove that driver with `pnputil /delete-driver oemNN.inf /uninstall` and run `Install.cmd` again. |
| Windows Update reports a failed `Red Hat - Display` update | The pin blocks driver installs on this device, so Windows Update cannot replace this package. This is intended ([installer.md](installer.md)). Uninstall removes the pin. |
| Device Manager's "Update driver" fails with "prohibited by policy" | The same pin. |
| Vulkan lists no device from `vmlab exec` | Vulkan does not enumerate from the SYSTEM session. Run tests in a user session (`tests/guest/Run-Probe.ps1`). |

## Evidence

`docs/evidence/package/README.md`: a fresh-clone build, then install,
probes, rerun, uninstall and reinstall on a fresh `windows-11` lab.
