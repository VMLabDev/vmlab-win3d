# Known limits

What the package does not do, does badly, or has not been tested on. Each
entry names its evidence. "Prior art" means `docs/prior-art.md`, a review of
an earlier attempt whose findings were not repeated here unless this page
says so.

## Host

### AMD only, for now

Venus works on the host's AMD iGPU (RADV, `/dev/dri/renderD129`) and fails on
the NVIDIA RTX 5090 (driver 610.57.04).

- With Venus on NVIDIA, the host's render server logs
  `vkr: vkGetImageMemoryRequirements resulted in CS error`, and in the guest
  vkcube fails to present
  (`wsi/win32: DXGI_SHARED CreateSwapChainForComposition failed (HRESULT 0x887a0005)`)
  with a black console.
  Evidence: `docs/host.md` Q2 and Q3,
  `docs/evidence/host/good-none/render-server-journal-summary.txt`,
  `docs/evidence/host/min-none/`.
- The prior art saw dwm crash before login on NVIDIA (prior-art §3, §8.1).
- The cause is unknown. Making NVIDIA work is wave 3 of `docs/plan.md`.

So a lab on a host with both must name the AMD GPU in `gpu.address`, which
makes vmlab put egl-headless on its render node and filter QEMU's Vulkan
loader to its driver. On a host with only an AMD GPU, the choice is moot. No
other host was tried.

### Requires vmlab 0.24.0-alpha or later

vmlab 0.23 did not give a working Venus guest from `gpu { mode = "vulkan" }`.
QEMU refused Venus without blob resources, and q35's extra VGA hid the Venus
device. The host GPU and its Vulkan driver could not be chosen, and
screenshots failed on GL displays (`docs/host.md`). Handoff
`20261017-142905` took those findings to vmlab, and 0.24.0-alpha fixes them.
`docs/host.md`, "Update: vmlab 0.24", lists what replaced each workaround.

With 0.24 or later, a lab needs only `secure_boot = false` and
`gpu { mode = "vulkan" address = "<PCI address of an AMD GPU>" }`, as in
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

### Presentation pacing is unmeasured

Nobody has measured frame pacing or console latency for this package. The
prior art measured its own builds of the same components (prior-art §3,
§2.4):

- D3D11 spent about 33 ms in each Present, on a composed GPU-to-GDI copy,
  for about 19 presents per second in GravityMark.
- D3D12 dropped about 40 % of its presents (667 of 1628 windowed), with
  about 105 ms display latency.

The probes here report frame rates, which are not a pacing measurement. In
`docs/evidence/package/README.md` they range from 19.6 fps (D3D9 x64) to
64.4 fps (OpenGL x86). Presentation only has to make a game watchable on the
console (`docs/brief.md`); deeper frame-pacing work is out of scope.

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

<!-- TODO(acceptance): fill from docs/acceptance.md once the e2e slice records the Windows 10 run. -->

**Status: not yet recorded.** `docs/acceptance.md` will hold the result of
the Windows 10 run from the local `windows-10` template (19045.2006.8). Until
it does, treat Windows 10 as untested. The prior art never tested Windows 10
either (prior-art §2). The D3D12 bridge was checked against the Windows 10
template's `d3d12.dll` export table at build time
(`build/d3d12/native-exports/win10-19045.2006.8-*.txt`), but it has not
run there.

### Application compatibility is barely tested

This package has run the five probes in `probes/` (D3D9, D3D11, D3D12,
OpenGL and Vulkan, x64 and x86) and nothing else. D3D10 has no probe here.
The prior art ran real applications on its own builds of the same components:

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
