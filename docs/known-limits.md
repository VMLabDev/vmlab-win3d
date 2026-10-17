# Known limits

What the package does not do, does badly, or has not been tested on. Each
entry names its evidence. "Prior art" means `docs/prior-art.md`, a review of
an earlier attempt whose findings were not repeated here unless this page
says so.

## Host

### Host GPU support

Venus runs the guest's Vulkan commands on the host's own Vulkan driver, so the
result depends on the host GPU. Four were tried:

| Host GPU, driver | Host | Result | Needs | Evidence |
| --- | --- | --- | --- | --- |
| AMD Radeon iGPU (Granite Ridge), RADV, Mesa 26.2.2 | helios | **Full pass**: every Done check in `docs/brief.md` item 2, on `windows-11` and `windows-10` | A plain vmlab 0.24 lab | [`acceptance.md`](acceptance.md) |
| Intel Iris Xe (Raptor Lake-P), ANV, Mesa 26.2.2 | Zeus | **Full pass** on `windows-11`, 12 of 12 Done checks. Without `patches/mesa/0021`, dwm crash-loops and the console is black | `patches/mesa/0021` (in the package) | [`zeus.md`](zeus.md) |
| NVIDIA RTX 5090, 610.57.04 | helios | **Partial**: all ten probes pass, vulkaninfo lists Venus, and the console shows the desktop and every probe. Not run through `tests/e2e/run.sh` | `patches/mesa/0020` (in the package) **and** the patched QEMU `patches/host/qemu/0001`, run through an isolated supervisor, until vmlab can name a QEMU binary (handoff `20261017-163910`, pending). The identical-GPU gap below applies | [`nvidia.md`](nvidia.md) |
| NVIDIA RTX 4060 Max-Q (laptop), 615.71.09 | Zeus | **Unmet**: install, desktop and console worked; then the GPU fell off the bus (Xid 79) during vulkaninfo and QEMU aborted. Cause unknown | Not known | [`zeus.md`](zeus.md) |

All four hosts ran QEMU 11.1.1 and virglrenderer 1.3.0. No other GPU or driver
version was tried.

**Without the patched QEMU, NVIDIA gives a black console.** NVIDIA's EGL
imports a LINEAR dma-buf only as an external texture, and QEMU 11.1.1's
egl-headless binds the guest's scanout as `GL_TEXTURE_2D`. The guest renders,
but the console and `vmlab vm screenshot` stay black. Without
`patches/mesa/0020` (bundles built before it), dwm crashes before logon.
`docs/nvidia.md` explains both faults.

**Two identical GPUs cannot be told apart.** On helios, vmlab's
`VK_LOADER_DRIVERS_SELECT=*nvidia*` matches both RTX 5090s, so the guest sees
two Venus devices with the same UUID. Applications picked GPU0, the one
`gpu.address` names. One that picks GPU1 would render on the other card while
egl-headless reads from the first. Handoff `20261017-163910` asks vmlab for a
way to pick one; it is pending.

> **Warning: the RTX 4060 failure needed a host reboot.** On Zeus, the NVIDIA
> GSP failed to boot during a Venus run (`Failed to boot GSP`, then Xid 119,
> Xid 79 "GPU has fallen off the bus"). QEMU aborted, `nvidia-smi` found no
> devices, and the GPU stayed unusable until Zeus rebooted. The evidence cannot
> separate runtime power management, the laptop's power state and the
> workload as the trigger. Do not run Venus on a laptop NVIDIA GPU that
> anything else depends on, and watch any repeat run
> (`docs/evidence/zeus/nvidia/kernel-nvrm.txt`).

On a host with an AMD or Intel GPU and an NVIDIA GPU, name the AMD or Intel
GPU in `gpu.address`. vmlab then puts egl-headless on its render node and
filters QEMU's Vulkan loader to its driver.

The `docs/host.md` probe, on vmlab 0.23 with workarounds, first found the
NVIDIA failure (`vkGetImageMemoryRequirements resulted in CS error` and a
black console; `docs/evidence/host/good-none/`, `min-none/`). It is
historical: `docs/nvidia.md` names the cause.

### Requires vmlab 0.24.0-alpha or later

vmlab 0.23 did not give a working Venus guest from `gpu { mode = "vulkan" }`.
QEMU refused Venus without blob resources, and q35's extra VGA hid the Venus
device. The host GPU and its Vulkan driver could not be chosen, and
screenshots failed on GL displays (`docs/host.md`). Handoff
`20261017-142905` took those findings to vmlab, and 0.24.0-alpha fixes them.
`docs/host.md`, "Update: vmlab 0.24", lists what replaced each workaround.

With 0.24 or later, a lab needs only `secure_boot = false` and
`gpu { mode = "vulkan" address = "<PCI address of an AMD or Intel GPU>" }`, as in
`labs/probe/vmlab.wcl`. On an older vmlab, QEMU refuses to start that VM
(`docs/evidence/host/up-plain.txt`).

The evidence in `docs/evidence/host/` and `docs/evidence/package/` was taken
on vmlab 0.23 with the workarounds (`docs/evidence/package/lab-vmlab.wcl`).

Two vmlab limits remain, with no fix:

- Online snapshots of a VM with virgl or Venus fail ("virgl is not
  migratable"). Offline snapshots work (prior-art §8.2).
- Vulkan does not enumerate in the SYSTEM session, so `vmlab exec` cannot run
  a GPU test directly. The tests use an autologon user and a scheduled task
  in session 1 (`tests/guest/Set-TestDesktop.ps1`, `tests/guest/Run-Probe.ps1`).

### Autologon values can vanish on a fresh clone

In one of the four `windows-11` acceptance runs, `AutoAdminLogon` and
`DefaultUserName` were gone from the Winlogon key after
`tests/guest/Set-TestDesktop.ps1 -Reboot`, before the package was installed.
`DefaultPassword` survived. Without those values nobody is logged on after a
reboot, and the session tests cannot start. The installer does not touch
Winlogon, and vmlab's repository has no code that does. The template agent's first-logon hooks are the likely
cause, but this is unconfirmed, and no vmlab handoff has been filed for it.
This affects only the tests, not the package: `tests/e2e/run.sh` records the
values before and after the install (`autologon-*.txt`) and sets them again
when they are missing. Evidence: `docs/acceptance.md`, "Findings from these
runs".

### QEMU crashes on shutdown

QEMU 11.1.1 sometimes dies with SIGSEGV in `libgallium` while tearing down
egl-headless. `vmlab down` still exits 0, and systemd-coredump writes a core.
In the host probe, 2 of 18 stops crashed, both in runs where Venus ran on
NVIDIA while EGL ran on AMD. The prior art also saw it on AMD alone after
heavy D3D12 work.

It loses no data: by the time `qemu_display_cleanup()` runs, the guest has
stopped and QEMU has flushed and closed the disks and the OVMF variables.
The fix is upstream QEMU commit `baca25172d8c195b9abbe6f8a3ab5915dba09a91`.
Decision D3 keeps the host on 11.1.1.

Evidence: `docs/host.md` Q6, `docs/evidence/host/q6-shutdown-crashes.txt`.

## Guest

### Test signing, Secure Boot off, and the Test Mode watermark

The KMD is signed with a test certificate made per checkout
([decision 0001](decisions/0001-signing.md)). As a result:

- The VM must run with `secure_boot = false`. With Secure Boot on, Windows
  refuses the `testsigning` boot option, and `Install.cmd` stops at its
  preflight. A guest cannot change this itself.
- The installer turns on `testsigning` and adds the certificate to the
  machine's Root and TrustedPublisher stores. The guest then accepts any
  kernel driver signed with a test certificate it trusts. That lowers its
  protection against boot kits and unsigned kernel code. Use it for lab VMs
  only.
- The desktop shows a "Test Mode" watermark in the bottom-right corner, which
  appears in every console screenshot.
- A guest must trust the certificate of the checkout that built its bundle.
  Bundles from two checkouts carry different certificates.

Attestation signing through Microsoft's Partner Center would remove all of
this, and it needs an EV certificate and a hardware developer account that
this project does not have.

### `d3d12.dll` is replaced

Windows has no registration point for a D3D12 implementation other than a
native D3D12 UMD, and this stack has none. The installer therefore replaces
`System32\d3d12.dll` and `SysWOW64\d3d12.dll` with the bridge, keeping the
originals as `vmlab-win3d\d3d12-native.dll` and in
`C:\ProgramData\vmlab-win3d\backup\`.

Anything that restores Microsoft's file, such as a cumulative update or
`sfc /scannow`, turns D3D12 back into Microsoft's runtime until the next
boot. At every boot the `vmlab-win3d-repair` task checks both files, keeps a
new native file as `d3d12-native.dll`, and puts the bridge back. Between the
update and that boot, D3D12 applications get Microsoft's runtime, which
finds no D3D12 driver for this adapter. The repair was
checked once, in the package slice's earlier lab, and that evidence was not
kept (`docs/evidence/package/README.md`, "Earlier iteration").

The bridge also has functional limits (`build/d3d12/README.md`):

- `D3D12DeviceRemovedExtendedData` is always empty, so DRED shows nothing.
- Only the DXGI factory class that `CreateDXGIFactory2(0)` returns is hooked.
  A debug factory (`DXGI_CREATE_FACTORY_DEBUG`) may not be.
- An application that ships its own `D3D12Core.dll` through the Agility SDK
  has not been tried.

### No native D3D12 DDI

The KMD's INF registers no D3D12 UMD, and Yttrium's `yttrium12.dll` slot is
not a working D3D12 UMD (prior-art §1). D3D12 works only through the bridge
and vkd3d-proton, so it depends on the `d3d12.dll` replacement above. Building
a native DDI is out of scope (`docs/brief.md`).

### Fast Startup and hibernation are off

With Fast Startup on, "Shut down" hibernates the kernel session, and a driver
change before it comes back stale. `vmlab vm stop` and `vmlab down` send an
ACPI power-off, which Windows treats as such a shutdown. The installer runs
`powercfg /hibernate off`, which turns off both, and uninstall restores the
earlier setting. Do not turn hibernation back on while the package is
installed. Evidence: decision 0001, "Fast Startup and hibernation hazard";
`docs/evidence/package/00-baseline-installer-state.txt` (the template already
had hibernation off).

### Presentation pacing is unmeasured, and D3D9 runs at about 30 fps

Nobody has measured frame pacing or console latency for this package. The
prior art measured its own builds of the same components (prior-art §3,
§2.4):

- D3D11 spent about 33 ms in each Present, on a composed GPU-to-GDI copy,
  for about 19 presents per second in GravityMark.
- D3D12 dropped about 40 % of its presents (667 of 1628 windowed), with
  about 105 ms display latency.

The probes here report frame rates, which are not a pacing measurement. On
the AMD host, the D3D9 probe reached about 30 fps in every acceptance run
(29.4 to 34.4 fps), against about 60 for D3D11, D3D12 and Vulkan and about 66
for OpenGL (`docs/evidence/acceptance/*/results.txt`). On Zeus's Intel GPU the
same D3D9 probe reached 59.3 and 58.0 fps (`docs/zeus.md`), so the halving is
not inherent to D3D9 on this stack. Its cause was not investigated. The
earlier package run on vmlab 0.23 measured 19.6 fps for D3D9 x64
(`docs/evidence/package/README.md`). Presentation only has to make a game
watchable on the console (`docs/brief.md`); deeper frame-pacing work is out
of scope.

DirectDraw presents are slow too: windowed blits to the primary reached 13 to
17 per second in `ddcaps` (`docs/dxdiag.md`, "Not covered").

### Kernel driver diagnostics

The KMD build has no tracing:

- WPP needs `tracewpp.exe`, which exists only on Windows.
  `build/kmd/compile-kmd.sh` replaces the generated `.tmh` files with empty
  stubs, so the driver has no ETW provider.
- The build is Release with no `DBG`, so the source's `DbgPrint` calls compile
  to nothing. The prior art ran a Debug build that printed to the kernel
  debugger and broke into it when one was attached.

To debug the KMD you need another build: a `DBG` build from this repository
does not exist yet, and upstream's Visual Studio project builds one on
Windows. See `build/kmd/README.md`.

### Windows 10

Windows 10 passes. On a fresh lab from the local `windows-10` template
(`x86_64/windows-10@19045.2006.8`) on the AMD host, every Done check in
`docs/brief.md` item 2 passed, 12 of 12, with the same package as the
Windows 11 run (`docs/acceptance.md`, commit `cb4d53c`;
`docs/evidence/acceptance/windows-10/`). The DirectX SDK D3D9 samples and
Blender also rendered there.

What was not run on Windows 10:

- the D3D11 sample fixes (`patches/mesa/0015` to `0017`, `docs/d3d11-apps.md`)
  and the Intel fix (`patches/mesa/0021`, `docs/zeus.md`), which came later;
- any host other than AMD;
- the uninstall and reinstall cycle, which ran on Windows 11 only
  (`docs/evidence/package/README.md`).

### Application compatibility is barely tested

This package has run the five probes in `probes/` (D3D9, D3D11, D3D12,
OpenGL and Vulkan, x64 and x86), two diagnostic probes (`probes/dxut-enum`,
`probes/d3d11-tess`), and a few unmodified applications
(`docs/acceptance.md`, "Stretch"; `docs/d3d11-apps.md`):

- DirectX SDK (June 2010) samples: `BasicHLSL` and
  `ParallaxOcclusionMapping` (D3D9) render; `SubD11` and `BasicHLSL11`
  (D3D11) render after `patches/mesa/0015` to `0017`, with the window
  caveat below;
- Blender 4.2.3 (OpenGL 4.3) renders its UI and viewport.

D3D10 has no probe here, and no D3D12 application other than the probe has
run (see below). The prior art ran real applications on its own builds of the
same components:

- GravityMark (D3D11, D3D12, OpenGL), Unigine Heaven (D3D9, D3D11, OpenGL),
  Godot 4.7.2 (Vulkan, D3D12) and Blender EEVEE (prior-art §2);
- 41,012 to 59,572 passing assertions of the vkd3d-proton D3D12 tests, not
  full conformance.

Those results do not carry over automatically. This repository re-exported
the prior art's Mesa fixes as patches and dropped its diagnostics and side
threads (`build/mesa/README.md`, `build/d3d12/README.md`). Apart from the
probes, none of the patched code has run in a guest since. Known application
issues from the prior art:

- Some applications need the Visual C++ runtime (GravityMark failed with
  `0xC0000135` without it). That is an application prerequisite, not a
  graphics fault.
- DXVK 3.1.1 has a clear-only black-frame bug in its D3D9/D3D11 path. This
  package uses only DXVK's DXGI, for D3D12, which the bug does not affect.

### DXUT (June 2010) D3D11 samples place their window off the screen

`BasicHLSL11` renders on our device, but its window lands at
(32758, −32768), off the screen, so the console shows nothing. `SubD11` lands
at (0, 0) in a 200×200 window. This is a bug in the June 2010 DXUT, not in
the package. On Windows 8 and later, without the SDK's reference rasterizer
`D3D11Ref.dll`, DXUT ends its enumeration on WARP's adapter, which has no
outputs, and then centres the window in an uninitialised `MONITORINFO`.
Current DXUT no longer does this. Workarounds: install the SDK's developer
runtime (`D3D11Ref.dll` in `System32`), or move the window from outside. The
samples still render on the hardware device either way. `D3D11Ref.dll` is not
redistributable, so the package cannot ship it. Evidence:
`docs/d3d11-apps.md`, "BasicHLSL11: a DXUT bug on Windows 8 and later".

### Domain shaders cannot index patch constants dynamically

A domain shader that indexes its patch constants with a register
(`dcl_indexrange vpc6.xyz 4`) is rejected by the UMD's shader translation
(`dcl_input_index_range` unsupported), and the D3D11 runtime then removes the
device. `patches/mesa/0015` added indexed patch constants and control points
for hull shaders only. SubD11 does not need it; the first version of
`probes/d3d11-tess` did (`docs/d3d11-apps.md`, "Not covered").

### No D3D12 application besides the probe

The planned D3D12 application, Microsoft's DirectX-Graphics-Samples
`D3D12HelloTriangle`, does not build unmodified with the project toolchain,
llvm-mingw. It uses the MSVC return-value form of
`GetCPUDescriptorHandleForHeapStart()`, WRL's `FileHandle` and `_uuidof`,
which DirectX-Headers and mingw do not provide for that compiler. Building it
needs MSVC or clang-cl with the Windows SDK, so it was skipped
(`docs/acceptance.md`, "Stretch"). D3D12 has run only as the probe here; the
prior art's D3D12 application results are listed above.

### DirectDraw fullscreen exclusive mode is untested

`patches/mesa/0012` to `0014` gave DirectDraw a hardware HAL, and `ddcaps`
checked it in windowed mode only. Fullscreen exclusive DirectDraw
(`DDSCL_EXCLUSIVE`, `Flip`) has not been tried. The UMD also offers no
hardware colour-key or mirror blits, so DirectDraw emulates them on the CPU
(`docs/dxdiag.md`, "Not covered").

### Other limits

- Only x64 Windows. The KMD is built for x64 only. 32-bit applications are
  covered through the x86 user-mode files in SysWOW64.
- The installer accepts only the hardware ID
  `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01`, the virtio-gpu device QEMU
  presents.
- If Windows Update brings a newer Red Hat `viogpudo` with a later driver
  date, it outranks this package for the same device. The installer then
  stops with the `pnputil /delete-driver` command to run (`docs/install.md`,
  "Troubleshooting").
