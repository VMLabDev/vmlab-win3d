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
| AMD Radeon iGPU (Granite Ridge), RADV, Mesa 26.2.2 | helios | **Full pass**: every Done check in `docs/brief.md` item 2, on `windows-11` and `windows-10` | A plain vmlab lab that names the AMD GPU (`host_gpu = "AMD1"`; the final runs used vmlab 0.25.0-alpha at commit `6105889`) | [`acceptance.md`](acceptance.md) |
| NVIDIA RTX 5090, 610.57.04 | helios | **Full pass**, 12 of 12 Done checks, through `tests/e2e/run.sh`: `windows-11` and `windows-10` on `Nvidia1` (vmlab 0.27.0-alpha) and `windows-11` on `Nvidia2` (vmlab 0.28.0-alpha); the guest lists exactly one Venus device. Depth draws into the back buffer need `patches/mesa/0022`, and mipmapped D3D9 textures need `0023` (below) | `patches/mesa/0020`, `0022` and `0023` (in the package) **and** the patched QEMU (the VMLabDev/qemu commit in `patches/host/PINS`) as the GPU's `qemu_binary` in the host config. Helios's host config gives both `Nvidia1` and `Nvidia2` one; without it the probes render but the console is black | [`acceptance.md`](acceptance.md#nvidia-rtx-5090), [`nvidia.md`](nvidia.md) |
| Intel Iris Xe (Raptor Lake-P), ANV, Mesa 26.2.2 | Zeus | **Full pass** on `windows-11`, 12 of 12 Done checks. Without `patches/mesa/0021`, dwm crash-loops and the console is black | `patches/mesa/0021` (in the package) | [`zeus.md`](zeus.md) |
| NVIDIA RTX 4060 Max-Q (laptop), 615.71.09 | Zeus | **Full pass**, 12 of 12 twice (2026-10-19, `bf4d948` of branch `work/nvfb`, package defaults, the patched QEMU, vmlab 0.28). On this host the guest does not see the GPU's writes to uncached host-visible memory (type 2) and the GPU does not see the guest's; without the fixes every Vulkan, D3D12 and OpenGL client hung on its first fence (Venus feedback lived there), and with only the feedback fix about one OpenGL readback in three came back black (Zink's upload buffers live there). The first D3D12 run in a fresh lab can present only one frame (not caused by the fixes) | `patches/mesa/0065` to `0067` (in the package) and the patched QEMU as `qemu_binary` | [`nvfb.md`](nvfb.md), [`zeus.md`](zeus.md#nvidia-rtx-4060) |

All four hosts ran QEMU 11.1.1 (on NVIDIA, the vmlab QEMU built on it) and
virglrenderer 1.3.0. No other GPU or driver version was tried. On
2026-10-18 the vmlab QEMU moved to upstream master (`d8a6048aae`), and
`windows-11` passed 12 of 12 with it on `Nvidia1`, `Nvidia2` and `AMD1`
(`docs/evidence/qemu-master/`).

**Without the patched QEMU, NVIDIA gives a black console.** NVIDIA's EGL
imports a LINEAR dma-buf only as an external texture, and stock QEMU's
egl-headless (11.1.1, and master as of 2026-10-18) binds the guest's
scanout as `GL_TEXTURE_2D`. The guest renders, but the console and
`vmlab vm screenshot` stay black. Each NVIDIA GPU a lab may use therefore
needs `qemu_binary` on its host config block
(`docs/nvidia.md`, "Host setup"). Without `patches/mesa/0020` (bundles built
before it), dwm crashes before logon. `docs/nvidia.md` explains both faults.

**On NVIDIA, depth draws into the back buffer hung the guest until
`patches/mesa/0022`, and mipmapped textures were black until `0023`.**
Before 0022, the DirectX SDK's D3D9 `BasicHLSL` hung on both RTX 5090s, and
the host logged an Xid 69 each time:

```
NVRM: Xid (PCI:0000:01:00): 69, pid=<render server>, name=vkr-ring-117, Class Error: channel 0x00000052, Class 0000ce97, Offset 0000026c, Data 000000a8, ErrorCode 0000009c
```

The cause is in our UMD. Yttrium renders into the shared back buffer, which
is a LINEAR image. NVIDIA 610.57.04 advertises no colour-attachment support
for LINEAR images. Without a depth buffer the GPU tolerates it. With one, the
first draw is rejected and the application hangs. This hits any D3D9, D3D10
or D3D11 application that draws with depth into its back buffer. 0022 renders
those passes through an OPTIMAL shadow image. With it, BasicHLSL and
ParallaxOcclusionMapping run on `Nvidia1` with no Xid, and the probes still
pass 10 of 10 on `Nvidia1` and `AMD1` ([`xid69.md`](xid69.md)).

Meshes that sample mipmapped textures (BasicHLSL's model,
ParallaxOcclusionMapping's disc) rendered black on NVIDIA with 0022 alone.
Every mipmapped D3D9 colour texture outside the system-memory pools was a
LINEAR image, and NVIDIA allows LINEAR images with one mip level only. 0023
makes such textures OPTIMAL when the host refuses LINEAR. Both samples now
render textured on `Nvidia1` and `AMD1`, and the probes pass 10 of 10 on both
([`nvtex.md`](nvtex.md)). The other stretch applications have not
run on NVIDIA. The acceptance evidence that shows the hang is
`docs/evidence/acceptance/windows-11-nvidia1/apps/`; the `Nvidia2` run's
`windows-11-nvidia2/apps/` is in git history at `a58acd3`. `run.sh` collects
the Done evidence before the stretch applications, so a hang there costs only
the application results.

> **The RTX 4060 once needed a host reboot.** On 2026-10-17 the NVIDIA GSP
> failed to boot during a Venus run on Zeus (Xid 119, then Xid 79 "GPU has
> fallen off the bus"), and only a reboot recovered it
> (`docs/evidence/zeus/nvidia/kernel-nvrm.txt`). It did not recur on
> 2026-10-19 across 52 runtime-power resumes under Venus, with or without a
> keep-awake helper. The 4060's other failure was a different one: Venus
> feedback writes from the host GPU never reached the guest, so Vulkan, D3D12
> and OpenGL clients hung. `patches/mesa/0065` to `0067` fix it
> ([nvfb.md](nvfb.md)).

On a host with an AMD or Intel GPU and an NVIDIA GPU, declare both in the host
config and name the AMD or Intel GPU in the lab with `host_gpu`. vmlab then
puts egl-headless on its render node and filters QEMU's Vulkan loader to its
driver. A lab that leaves out `host_gpu` gets the host's default GPU. On
helios the default is `Nvidia1`, an RTX 5090 with the patched QEMU as its
`qemu_binary`; the labs here name their GPU explicitly.

The `docs/host.md` probe, on vmlab 0.23 with workarounds, first found the
NVIDIA failure (`vkGetImageMemoryRequirements resulted in CS error` and a
black console; `docs/evidence/host/good-none/`, `min-none/`). It is
historical: `docs/nvidia.md` names the cause.

### Two Windows VMs per RTX 5090 without resizable BAR

Helios's RTX 5090s run with resizable BAR off, so each card has a 256 MiB
BAR1 aperture that every process and VM on the card shares. Venus guests
fill it. Each Windows VM takes about 40 MiB at an idle desktop and 60 MiB
while it renders, mostly for the Yttrium UMD's shared images. With three VMs
on a card, BAR1 runs out: the host kernel logs `NVRM: dmaAllocMapping_GM107:
can't alloc VA space for mapping`, and guest Venus contexts die (dwm or
applications report ring timeouts, often in `vkGetImageSubresourceLayout`).
`patches/mesa/0070` (with `0086`) keeps Vulkan, DXVK, vkd3d-proton and Zink
memory out of BAR1, but not the UMD's images.

Run at most two Windows VMs per RTX 5090, counting the host desktop on
`Nvidia1` as part of the load, or turn on resizable BAR in the host's
firmware. Integrated GPUs and resizable-BAR hosts are not affected. Cause,
numbers and the dropped UMD change: [nvva.md](nvva.md).

### Requires vmlab 0.27.0-alpha or later

vmlab 0.23 did not give a working Venus guest from `gpu { mode = "vulkan" }`.
QEMU refused Venus without blob resources, and q35's extra VGA hid the Venus
device. The host GPU and its Vulkan driver could not be chosen, and
screenshots failed on GL displays (`docs/host.md`). Handoff
`20261017-142905` took those findings to vmlab, and 0.24.0-alpha fixes them.
`docs/host.md`, "Update: vmlab 0.24", lists what replaced each workaround.

vmlab 0.25.0-alpha then moved the GPU's PCI address out of the lab file into
the host config, `~/.config/vmlab/config.wcl`, and rejects `address` and
`vulkan_icd` in a lab's `gpu {}` (`docs/host.md`, "Update: vmlab 0.25"). The
lab files here use the 0.25 form, so they need 0.25.0-alpha or later. A lab
needs only `secure_boot = false` and
`gpu { mode = "vulkan" host_gpu = "<name of an AMD or Intel GPU>" }`, as in
`labs/probe/vmlab.wcl`, and the host config must declare that name:
`gpu "AMD1" { address = "0000:72:00.0" }` on helios. On vmlab 0.23, QEMU
refuses to start that VM (`docs/evidence/host/up-plain.txt`).

vmlab 0.26.0-alpha added `qemu_binary` and `qemu_env` to a host GPU block,
which NVIDIA needs for its console, and 0.27.0-alpha added the PCI-filter
Vulkan layer that leaves the guest only the named GPU (`docs/host.md`,
"Update: vmlab 0.26 and 0.27"). An AMD or Intel lab still works on 0.25,
but a guest on a host with two GPUs of the same driver sees both before
0.27. The NVIDIA acceptance runs used 0.27.0-alpha.

The evidence in `docs/evidence/host/` and `docs/evidence/package/` was taken
on vmlab 0.23 with the workarounds (`docs/evidence/package/lab-vmlab.wcl`).
The acceptance, NVIDIA and Zeus evidence was taken on vmlab 0.24, with the
GPU named by `gpu.address` in the lab file.

Two vmlab limits remain, with no fix:

- Online snapshots of a VM with virgl or Venus fail ("virgl is not
  migratable"). Offline snapshots work (prior-art §8.2).
- Vulkan does not enumerate in the SYSTEM session, so `vmlab exec` cannot run
  a GPU test directly. The tests use an autologon user and a scheduled task
  in session 1 (`tests/guest/Set-TestDesktop.ps1`, `tests/guest/Run-Probe.ps1`).

### Autologon values can vanish on a fresh clone

On fresh `windows-11` clones, `AutoAdminLogon` and `DefaultUserName` were
sometimes gone from the Winlogon key after
`tests/guest/Set-TestDesktop.ps1 -Reboot`, before the package was installed.
`DefaultPassword` survived. Without those values nobody is logged on after a
reboot, and the session tests cannot start.

vmlab found the cause (handoff `20261018-092229`): each clone of a
sysprep-generalized template replays Windows' user OOBE, and when OOBE exits,
`CloudExperienceHostBroker.exe` deletes those values, whoever wrote them.
The published templates' first-boot script can report the clone ready
before that happens. vmlab-templates fixed the script at `fb0c538`, but the
local templates predate it, and this project does not rebuild templates.

`Set-TestDesktop.ps1` now waits for OOBE to finish, with vmlab's check,
before it writes the values, and prints an `oobe:` line saying how long it
waited (`test-desktop.txt`). `tests/e2e/run.sh` still records the values
before and after the install (`autologon-*.txt`) and sets them again when
they are missing, logging `autologon fallback fired` and writing
`autologon-fallback.txt`. In the five runs since, on `Nvidia1` and
`Nvidia2`, the wait ended at once or within 5 s, and the fallback did not
fire. This affects only the tests, not the package.

### Evaluation templates need the lab NIC and `slmgr /ato`

The local `windows-11` and `windows-10` templates are Enterprise
Evaluation editions, and their clones boot in notification mode (`slmgr /dlv`: `License Status:
Notification`, `0xC004F009 (grace time expired)`). In that mode the license
monitor, `wlms.exe`, shuts the VM down an hour after each boot, with "The
license period for this installation of Windows has expired". A lab with no
NIC cannot activate, so a kept lab dies within the hour.

The lab files therefore give the VM a NAT NIC, `nic { nat = true }`
(`labs/acceptance/vmlab.wcl`, `labs/probe/vmlab.wcl`). The NIC alone is not
enough. A clone left alone with it was not activated by Windows, and
`wlms.exe` shut it down 60 minutes after it booted. Activation needs an
explicit `slmgr /ato`, which takes about a minute:

```
cscript //nologo C:\Windows\System32\slmgr.vbs /ato
```

After that, `slmgr /dlv` shows `License Status: Licensed` and
`slmgr /xpr` shows the evaluation's end date, 90 days later (`Timebased
activation will expire 1/16/2027`). Activation survives a reboot, and the
activated clone ran for over an hour without shutting down. `tests/e2e/run.sh` runs
`slmgr /ato` as soon as the lab is up and writes the result to
`activation.txt` in the run's evidence; a failed activation is reported but
does not fail the run. For a lab brought up by hand, run the command above
once.

The NIC changes what the guest can reach: it has internet egress through
vmlab's NAT, and the lab's guests can reach each other. The template's
Windows Update policies stay in place, so the NIC does not turn Windows
Update on. The rearm count is 1, so `slmgr /rearm` is not a lasting
alternative. Tried on `windows-11@26100.1742.5` and, for `slmgr /ato`
only, `windows-10@19045.2006.8`.

Evidence: `docs/evidence/wu/activation/` (`no-ato-shutdown.txt`, the
unactivated clone's shutdown events; `ato.txt`, activation and the
reboot; `windows-10-ato.txt`), and the `activation` report of each run in `docs/evidence/wu/`.

### QEMU crashes on shutdown

QEMU 11.1.1 sometimes dies with SIGSEGV in `libgallium` while tearing down
egl-headless. `vmlab down` still exits 0, and systemd-coredump writes a core.
In the host probe, 2 of 18 stops crashed, both in runs where Venus ran on
NVIDIA while EGL ran on AMD. The prior art also saw it on AMD alone after
heavy D3D12 work.

It loses no data: by the time `qemu_display_cleanup()` runs, the guest has
stopped and QEMU has flushed and closed the disks and the OVMF variables.
The fix is upstream QEMU commit `baca25172d8c195b9abbe6f8a3ab5915dba09a91`.
Decision D3 keeps the host's own QEMU on 11.1.1, so labs that run the stock
QEMU, which by default is every AMD and Intel lab, can still crash this way.

**The vmlab QEMU fixes it.** Since `patches/host/PINS` moved to
`d8a6048aae` (2026-10-18), the vmlab QEMU is the series on upstream QEMU
master, which already has `baca25172d`. A host GPU whose host config block
names it as `qemu_binary` no longer crashes on shutdown. With that build,
10 stop and start cycles on `AMD1` (where every stock-QEMU core on helios
came from) and 10 on `Nvidia2` all ended in `exit status: 0`, with no core
dump, each after the D3D12 and D3D11 probes had run
(`docs/evidence/qemu-master/`).

Evidence: `docs/host.md` Q6, `docs/evidence/host/q6-shutdown-crashes.txt`,
`docs/evidence/qemu-master/stock-qemu-shutdown-cores.txt`, and for the fix
`docs/evidence/qemu-master/cycles-amd1/cycles.log`.

### The QEMU tarball suits current Arch-based hosts, and 0.1.0-alpha's needs AVX-512

The vmlab QEMU tarball links the target host's shared libraries, built
against helios's: it needs a current Arch Linux or CachyOS with glibc 2.43
or later and every library in its `BUILDINFO` at the same version or
newer. The CPU needs x86-64-v2. `install.sh --check` checks the libraries,
the CPU's ISA level and `--version`. On other distributions, build QEMU on
the host with `patches/host/build-qemu.sh`.

0.1.0-alpha's tarball (`qemu-11.1.1-vmlab-egl-0.1.0-alpha-...`) is marked
`x86 ISA needed: x86-64-v4`, so it does not start on a CPU without
AVX-512. On Zeus (i7-13700H) it fails with
`CPU ISA level is lower than required`, while its own `install.sh --check`
passes. Later builds target x86-64-v2 (`docs/release.md`, "CPU and library
baseline of the QEMU binary").

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

### Presentation pacing is unmeasured; D3D9 is below the refresh rate on NVIDIA

Nobody has measured frame pacing or console latency for this package. The
prior art measured its own builds of the same components (prior-art §3,
§2.4):

- D3D11 spent about 33 ms in each Present, on a composed GPU-to-GDI copy,
  for about 19 presents per second in GravityMark.
- D3D12 dropped about 40 % of its presents (667 of 1628 windowed), with
  about 105 ms display latency.

The probes here report frame rates, which are not a pacing measurement.

D3D9 ran at about 30 fps on the AMD host and about 20 fps on its RTX 5090,
and DirectDraw windowed blits to the primary reached 13 to 34 per second.
On hosts with an AMD CPU, the KMD's CPU copy for a Blt present wrote an
uncached destination and read an uncached source. `patches/kmd/0002` and
`patches/mesa/0026` fix both ends ([`present.md`](present.md)). On
`AMD1` the D3D9 probe now runs at 59.4 and 58.2 fps (x64, x86), and
DirectDraw at several hundred Blts per second.

On `Nvidia1` the D3D9 probe rose from about 22 to 39 fps, still below the
refresh rate. There each windowed D3D9 Present waits about 16 to 20 ms,
whatever the window size, with the CPU idle. The cause is on the host
([`n5.md`](n5.md)): virglrenderer creates its Venus queue-sync fences
exportable, and NVIDIA 610.57.04 wakes `vkWaitForFences` on such fences up to
about 10 ms late. Every guest DMA buffer therefore completes at least 10 ms
after submission, and a D3D9 Present waits for two of them. A one-line
virglrenderer patch removes the delay (ring fences 10.2 ms to 1.2 ms, the
D3D9 probe 37 to 47 fps on a shared GPU), but it is not deployed: helios
runs the stock render server (decision D3). Presentation only has
to make a game watchable on the console (`docs/brief.md`); deeper
frame-pacing work is out of scope.

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
`docs/brief.md` item 2 passed, 12 of 12. It ran the same package as the
Windows 11 run: commit `6105889`, with every Mesa patch including 0015 to
0017 and 0021 (`docs/acceptance.md`; `docs/evidence/acceptance/windows-10/`).
The DirectX SDK D3D9 and D3D11 samples and Blender also rendered there.

Blender's window was translucent on Windows 10: the wallpaper showed through
its viewport and panels (`docs/evidence/acceptance/windows-10/apps/console-opengl-x64-Blender.png`).
Mesa's WGL asked for a premultiplied-alpha swapchain for every window, so
the compositor blended the alpha Blender leaves in its framebuffer.
`patches/mesa/0027` presents WGL windows opaque ([`present.md`](present.md)).

What was not run on Windows 10:

- any host other than AMD;
- the uninstall and reinstall cycle, which ran on Windows 11 only
  (`docs/evidence/package/README.md`).

### Application compatibility is barely tested

This package has run the five probes in `probes/` (D3D9, D3D11, D3D12,
OpenGL and Vulkan, x64 and x86), two diagnostic probes (`probes/dxut-enum`,
`probes/d3d11-tess`), and a few unmodified applications
(`docs/acceptance.md`, "Stretch"; `docs/d3d11-apps.md`):

- DirectX SDK (June 2010) samples: `BasicHLSL` and
  `ParallaxOcclusionMapping` (D3D9) render, textured on NVIDIA since
  `patches/mesa/0023`; `SubD11` and `BasicHLSL11`
  (D3D11) render after `patches/mesa/0015` to `0017`, with the window
  caveat below;
- Blender 4.2.3 (OpenGL 4.3) renders its UI and viewport.
- The compatibility sweep ([compat.md](compat.md)) runs Unigine Heaven and
  Valley (D3D11, D3D9, OpenGL), Godot 4.7.2 (D3D12, Vulkan, OpenGL), GZDoom
  (Vulkan, OpenGL) and Crispy Doom (D3D9), each for about 3 minutes on AMD1
  and Nvidia2:
  - AMD1: 10 of 12 runs pass. Valley D3D9 hangs, and GZDoom cannot create
    an OpenGL context. Heaven D3D11 crashed once with heap corruption.
  - Nvidia2: 5 of 12 runs pass. Godot's Vulkan renderer causes a host Xid 69
    (ErrorCode `0x13d`). GZDoom Vulkan and both Unigine OpenGL runs crash.
    Unigine D3D9 skips its draws that use depth bias, because the device
    lacks the float depth-bias representation.

D3D9 fixed function goes through the shaders that the Windows runtime
generates; its coverage and what it still lacks are in the next section.

D3D10 has no probe here. Five unmodified Microsoft D3D12 samples have run
([d3d12app.md](d3d12app.md); all five render, `D3D12ExecuteIndirect` with `patches/vkd3d-proton/0006`, [ei.md](ei.md)). The prior art ran real applications on its own builds of the
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

### D3D9 fixed function: what is and is not covered

`docs/evidence/ffp/src/d3d9_ffp.cpp` checks 168 fixed-function cases
against Microsoft's reference rasterizer: texture-stage blending with up to
four stages, texture transforms and coordinate generation, vertex and table
fog, lighting, alpha test and blending, point sprites, and transformed and
pre-transformed vertices, with hardware and with software vertex
processing ([`ffp.md`](ffp.md)). With `patches/mesa/0040` to `0042` every
case matches on `Nvidia2` and `AMD1` except two spotlight cases, where the
Windows runtime's own vertex processing differs from the reference's by up
to 12/255 on a few pixels; the reference fed by the runtime's software
vertex processing matches the driver exactly. Not tested: `PREMODULATE`,
W-buffering, multisampling and wide lines.

`docs/evidence/ffp2/src/d3d9_ffp2.cpp` adds 49 cases ([`ffp2.md`](ffp2.md)):
vertex blending with up to three weights, indexed blending and tweening,
bump environment mapping, cube and volume texture coordinates, and mixed
devices that switch between hardware and software vertex processing. Both
probes also ran on Windows 10. With `patches/mesa/0043` (the `L6V5U5` and
`X8L8V8U8` bump maps) every case matches on `Nvidia2` and `AMD1`. The
exception is `vb_indexed_color`, where the reference's own vertex
processing loses the device. Limits:

- with hardware vertex processing, indexed vertex blending works for matrix
  indices 0 to 8 only. That is the palette of the runtime's generated
  shader, and `MaxVertexBlendMatrixIndex` reports 8 (`0044`). Larger
  palettes need software or mixed vertex processing;
- a dynamic `D3DPOOL_DEFAULT` texture in `L6V5U5` or `X8L8V8U8` shows
  `Lock` the converted `R8G8B8A8_SNORM` layout.

Table fog chooses W fog from the projection's W range, which the runtime
reports through `UpdateWInfo`; a perspective projection reported with
`WNear == WFar` would get Z fog.

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

### Indexed shader inputs and outputs: what is covered

Shaders may index their inputs and outputs with a register
(`dcl_indexrange`). `patches/mesa/0015` covers hull shader patch phases,
`0030` patch constants in domain shaders ([`ds.md`](ds.md)), `0031` output
ranges in every other stage and in a hull shader's control-point phase, and
`0032` hull shader join phases that read their fork phases' outputs
([`shaders2.md`](shaders2.md)). Before `0032`, any such join phase removed
the device, even with immediate indices; d3dcompiler_47 emits one whenever a
patch-constant function computes one output from another. Vertex, domain,
geometry and pixel shader input ranges, tessellation-factor ranges, and
output ranges in every stage match WARP on AMD1 and Nvidia1. What remains is
in [`shaders2.md`](shaders2.md#what-remains): a range over a geometry
shader's packed scalar system values is still rejected, and indexed reads of
ordinary patch constants in a join phase were not run.

### D3D12: ExecuteIndirect with vertex or index buffer arguments, and an NVIDIA first-boot dwm crash

Five unmodified Microsoft DirectX-Graphics-Samples D3D12 samples now build
with clang-cl and the MSVC CRT (`build/d3d12-samples/`) and run through the
installed package with no app-local DLLs (`docs/d3d12app.md`).
`D3D12HelloTriangle`, `D3D12HelloTexture`, `D3D12Bundles` and
`D3D12nBodyGravity` render and exit 0 on AMD1 and Nvidia1. Two failures
remain:

- **`ExecuteIndirect` with a command signature that sets vertex or index
  buffer views** draws wrongly, with no error. Venus (guest Mesa 26.1.0 and
  virglrenderer 1.3.0) does not forward `VK_EXT_device_generated_commands`,
  which vkd3d-proton uses for state-changing signatures.
  `patches/vkd3d-proton/0006` emulates signatures that set root constants
  or root CBV, SRV or UAV addresses: one GPU-patched indirect draw or
  dispatch per sequence. With it, `D3D12ExecuteIndirect` renders as on WARP
  on AMD1 and Nvidia1 ([ei.md](ei.md)). Vertex and index buffer view
  arguments are still dropped. The emulation also moves every root
  signature's root parameters into a push UBO and root CBVs to raw VAs,
  with unmeasured cost per draw.
- **On NVIDIA, dwm.exe can crash in the first boot after the install.** The
  crash is heap corruption, `0xc0000374`, with `viogpu_d3d10.dll` loaded,
  during the first D3D12 sample. D3D12 programs then fail until the next
  reboot. It happened in two fresh Nvidia1 labs and in neither AMD1 lab. One
  more reboot clears it. The cause is open.

### DirectDraw: 16 bpp only for 32-bit programs, and flips below the refresh rate on NVIDIA

Fullscreen exclusive DirectDraw works: the mode change, the flip chain,
`Flip`, and restoring the desktop mode. `patches/mesa/0035` draws source
colour-key and mirror blits on the GPU, matching DirectDraw's emulation
pixel for pixel ([`ddraw.md`](ddraw.md)). What remains:

- **16 bpp in 64-bit programs.** Windows' 32-bit `user32.dll` and
  `ddraw.dll` emulate 8 and 16 bpp, so 32-bit games get their 16-bit modes.
  A 64-bit DirectDraw program gets `DDERR_UNSUPPORTED` for 16 bpp, and no
  driver can change that: `dxgkrnl.sys` drops low-colour source modes from
  every WDDM driver's mode list, and 64-bit Windows has no low-colour
  emulation ([`dd16.md`](dd16.md)).
- **Speed.** Each `Flip` is a Blt-model present with the same cost as a
  D3D9 present ("Presentation pacing" above). With the present fixes
  (package 0.1.1-alpha-rc1), fullscreen 32 bpp flips 40 to 44 times per
  second on Nvidia1 and 58 to 60 on AMD1; x86 16 bpp, 17 to 18 and 34 to
  43. Before them it was 2 to 8 and 9 to 29 ([`ddraw.md`](ddraw.md#frame-rate)).
- **Still emulated.** Destination colour keys and rotated blits still run
  on the CPU.

### Other limits

- Only x64 Windows. The KMD is built for x64 only. 32-bit applications are
  covered through the x86 user-mode files in SysWOW64.
- The installer accepts only the hardware ID
  `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01`, the virtio-gpu device QEMU
  presents.
- While the package is installed, no other driver can be installed on the
  display device. The installer pins the device instance with the device
  installation policy `DenyInstanceIDs`, so a newer Red Hat `viogpudo` from
  Windows Update cannot replace this package. Windows Update reports that
  install as failed, and Device Manager's "Update driver" is refused. The
  INF's `FeatureScore=E0` already outranks `viogpudo` whatever its date, and
  the installer forces the package on when PnP prefers another driver.
  Uninstall removes the pin ([installer.md](installer.md)).
