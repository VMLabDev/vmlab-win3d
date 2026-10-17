# Evidence: package build, install, rerun, uninstall and reinstall

Date: 2026-10-17. This run checks the `package` slice's acceptance criteria
on a fresh lab, with the bundle built from a fresh clone.

## Build from a fresh clone

`git clone --branch work/package` into a new directory at `f7fa236`, then
`scripts/build.sh` with an empty `out/` and no `.signing/`. Every component
build ran, the run created a new test certificate, and the bundle was
assembled and signed. Exit 0. See `00-fresh-clone-build.txt`: the stage
times, the certificate, `manifest.json` and the sha256 of every bundle file.

## Lab

`lab-vmlab.wcl` is `labs/probe/vmlab.wcl` (merged from host-probe) with the
lab name `win3d-package-b`: the local `windows-11` template 26100.1742.5,
`secure_boot = false`, `gpu { mode = "vulkan" }`, `-global
virtio-gpu-gl-device.blob=true`, `-vga none` and `-display
egl-headless,rendernode=/dev/dri/renderD129`. vmlab ran against an isolated
supervisor with `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`
(AMD iGPU, RADV). The lab was created fresh for this run and destroyed after
it.

`scripts/test-package-in-lab.sh` from the same fresh clone drove the run,
with `SHOT_CMD` set to `labs/probe/tools/vnccap.py` on the VM's `vnc.sock`.
`run.log` is its output. Steps and evidence:

| Step | Evidence | Result |
| --- | --- | --- |
| 00 test user with autologon (`tests/guest/Set-TestDesktop.ps1`), then the baseline | `00-test-desktop.txt`, `00-baseline-gpu-state.json`, `00-baseline-installer-state.txt` | Red Hat VirtIO GPU DOD controller, `oem7.inf` 100.101.104.28500, problem code 0. No testsigning entry. Vulkan loader 1.3.300 with no ICD. Hibernation already off. |
| 01 install (`scripts/install-into-vm.sh`) | `01-install.log` | `Installed` after two reboots: one for test signing, one at the end. |
| 02 state after install | `02-installed-gpu-state.json`, `02-installed-installer-state.txt`, `02-journal.log` | Below |
| 03 probes (`tests/guest/Run-AllProbes.ps1`) | `03-probes.txt`, `03-probes-summary.json`, `console-*.png` | 10 of 10 pass |
| 04 install again | `04-reinstall.log`, `05-reinstalled-*` | `nothing changed; no reboot needed`. State identical to 02. |
| 06 uninstall | `06-uninstall.log`, `07-journal.log` | `Removed` after one reboot |
| 07 state after uninstall, compared with 00 | `07-uninstalled-gpu-state.json`, `07-uninstalled-installer-state.txt`, `07-compare-baseline.txt` | `identical to the baseline` |
| 08 install after the uninstall | `08-install-again.log`, `09-installed-again-*` | `Installed`, two reboots. Same state as 02. |

## After install (02)

From `02-installed-gpu-state.json`:

- The display device is **Hardsoft VirtIO GPU 3D controller**, status `OK`,
  **problem code 0**, `DEVPKEY_Device_DriverInfPath` = `oem15.inf` (this
  package), driver version 100.6.101.58100.
- Class key: `UserModeDriverName` = `C:\WINDOWS\system32\viogpu_d3d10.dll`
  ×3, `UserModeDriverNameWow` = the SysWOW64 path ×3,
  `InstalledDisplayDrivers` = `viogpu_d3d10` ×3, `OpenGLDriverName[Wow]` =
  `viogpu_wgl.dll` in System32 and SysWOW64, `OpenGLFlags[Wow]` = 3,
  `OpenGLVersion[Wow]` = 4096. There is no D3D12 UMD entry.
- `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` holds
  `C:\WINDOWS\system32\virtio_icd.x86_64.json` = 0, and the WOW6432Node key
  holds `C:\WINDOWS\SysWOW64\virtio_icd.x86.json` = 0.
- `System32\d3d12.dll` and `SysWOW64\d3d12.dll` are the bridges (`b5d94f44…`,
  `944cc70e…`), owned by `NT SERVICE\TrustedInstaller` with the native ACL
  (`02-installed-installer-state.txt`). The native files are
  `vmlab-win3d\d3d12-native.dll`.
- `vulkan-1.dll` is the package's loader 1.4.363 in both directories,
  replacing the template's 1.3.300.
- testsigning `Yes`; the test certificate is in Root and TrustedPublisher;
  `vmlab-win3d-repair` is registered and `vmlab-win3d-resume` is gone.

## Probes (03)

Every probe ran as the logged-on user `win3d` in session 1, with no
application-local DLLs. `graphics_modules` in `03-probes-summary.json` lists
only `system32`, `syswow64` and `system_subdir` modules.

| Probe | x64 | x86 | Adapter |
| --- | --- | --- | --- |
| D3D9 | pass, 19.6 fps | pass, 29.9 fps | Hardsoft VirtIO GPU 3D controller |
| D3D11 | pass, 59.8 fps | pass, 57.8 fps | Hardsoft VirtIO GPU 3D controller |
| D3D12 | pass, 55.3 fps | pass, 56.5 fps | Hardsoft VirtIO GPU 3D controller |
| OpenGL | pass, 49.6 fps | pass, 64.4 fps | zink Vulkan 1.4 (Virtio-GPU Venus (… RADV RAPHAEL_MENDOCINO)) |
| Vulkan | pass, 59.7 fps | pass, 59.7 fps | Virtio-GPU Venus (AMD Ryzen 9 9950X … (RADV RAPHAEL_MENDOCINO)) |

All runs report `software: false` and `readback_ok: true`. The D3D12 probe
loads `System32\d3d12.dll` (the bridge), `System32\vmlab-win3d\vkd3dcore.dll`,
`vmlab-win3d\dxgi-vulkan.dll`, `vulkan-1.dll` and `libvulkan_virtio.dll`. The
D3D9 and D3D11 probes load `viogpu_d3d10.dll`, and OpenGL loads
`viogpu_wgl.dll`. The full pass is the e2e slice's job; this is what the
package slice saw.

`console-x64-d3d9.png`, `console-x64-d3d11.png`, `console-x64-d3d12.png`,
`console-x64-opengl.png`, `console-x64-vulkan.png` and
`console-x86-d3d12.png` are VNC captures of the vmlab console taken while
the probes ran, scaled to half size. `vmlab vm screenshot` fails with
`no surface` under egl-headless (`docs/host.md` Q4).

## Uninstall back to the baseline (07)

`07-compare-baseline.txt` compares `00-baseline-gpu-state.json` with
`07-uninstalled-gpu-state.json` (`scripts/compare-gpu-state.py`, which skips
only the collection time and video mode): **identical to the baseline**. That
covers the display driver (`oem7.inf`, problem code 0), every class-key
value, the Vulkan registry in both views, the version and sha256 of every
system graphics DLL, and the boot policy. In particular:

| File | Baseline | Installed | After uninstall |
| --- | --- | --- | --- |
| `System32\d3d12.dll` | `6b321815…` | `b5d94f44…` (bridge) | `6b321815…` |
| `SysWOW64\d3d12.dll` | `a43885b0…` | `944cc70e…` (bridge) | `a43885b0…` |
| `System32\vulkan-1.dll` | `591d4c31…` | `eae9c602…` | `591d4c31…` |
| `SysWOW64\vulkan-1.dll` | `411a6f91…` | `1b8122e8…` | `411a6f91…` |

The restored `d3d12.dll` files are owned by TrustedInstaller with their
original ACL (`07-uninstalled-installer-state.txt`). The installer's own
verification after the reboot passed as well (`07-journal.log`, `removed;
the machine matches its baseline`): no viogpu3d package is left in the
driver store, the `VioGpu3D` and `vgpusrv` services are deleted, the private
directories are gone, and the certificate and testsigning entry are removed.

## Earlier iteration

An earlier lab, `win3d-package-a`, used the prior-art display line
(`virtio-vga-gl,hostmem=4G,blob=on,venus=on` with a memfd RAM backend). It
found three installer bugs, which are fixed in this bundle:

- the package's `vulkan-1.dll` has the FileVersion string "Vulkan Loader -
  Dev Build", so the version check now reads the binary file version;
- restricting the ACL before setting the owner removed our own
  WRITE_OWNER; owner and DACL are now set in one `Set-Acl`;
- PnP keeps the device's class key for the next driver, so the INF's
  `UserModeDriverName` and `OpenGL*` values outlived the uninstall. The
  baseline now records them and uninstall restores them.

The same lab also checked the repair task: after `System32\d3d12.dll` was
replaced by the native file, `vmlab-win3d-repair` put the bridge back with
the TrustedInstaller owner. Its full rerun passed with the same results as
above. That evidence is not kept.
