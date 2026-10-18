# Zeus: the package on an Intel iGPU and a laptop RTX 4060

This page records how the package behaves on Zeus, the user's laptop, on
its two GPUs. It gives the host, the lab files, the exact commands, a
pass/fail table per GPU, and the cause of each failure. Every claim cites a
file under `docs/evidence/zeus/`. Date: 2026-10-17.

## Result

| GPU | Package | Result |
| --- | --- | --- |
| Intel Iris Xe (Raptor Lake-P), ANV | `main` (`db07da3`) | **Fails.** dwm crash-loops after the install, the console is black, and no probe shows. Cause below. |
| Intel Iris Xe (Raptor Lake-P), ANV | with `patches/mesa/0021` | **Passes**, 12 of 12 Done checks: vulkaninfo, all 10 probes on the console, dxdiag. R32 typed UAV loads work (uavtest). Fix below. |
| AMD iGPU on helios, RADV (regression) | with `patches/mesa/0021` | **Passes**, 12 of 12 Done checks, 10 of 10 probes. |
| NVIDIA RTX 4060 Max-Q, 615.71.09 | `main` (`db07da3`) + patched QEMU | **Stopped.** Install, desktop and console work. One minute later the GPU **fell off the bus** (Xid 79), and QEMU aborted. The GPU stays unusable until Zeus reboots. |

## Host

| Item | Value |
| --- | --- |
| Machine | Zeus, a laptop the user works on; i7-13700H, 20 threads, 93 GiB |
| OS | CachyOS, kernel 7.2.4-3-cachyos |
| GPUs | Intel Iris Xe (Raptor Lake-P) `0000:00:02.0`, `renderD129`; NVIDIA RTX 4060 Max-Q (AD107M) `0000:01:00.0`, `renderD128`, driver 615.71.09 |
| Host stack | QEMU 11.1.1, virglrenderer 1.3.0, Mesa 26.2.2 (`vulkan-intel`), the same as helios |
| vmlab | 0.24.0-alpha for every run on this page. It was 0.21.2-alpha, and the user authorised the upgrade (below). Zeus has no host config (`~/.config/vmlab/config.wcl`) yet |
| Template | `x86_64/windows-11@26100.1742.5`, pulled for this work |

`host.txt` in each evidence directory records these per run.

### What changed on Zeus

The user authorised these changes:

- vmlab was upgraded with the official installer,
  `curl -fsSL https://vmlab.io/install.sh | sh -s -- --version v0.24.0-alpha --no-fastpath`.
  This also installed the 0.24 guest assets in `~/.local/share/vmlab/guest`.
  The user's supervisor was restarted with `vmlab daemon stop` and
  `vmlab daemon start`, so it runs 0.24. The user then ran
  `sudo setcap cap_bpf,cap_net_admin+ep ~/.local/bin/vmlab` themselves. The
  supervisor I restarted (pid 2419267) started before that `setcap`, so it
  still uses the userspace network fast path until it restarts again.
- The templates `windows-11@26100.1742.5` and `windows-10@19045.2006.8` were
  pulled with `vmlab template pull --arch x86_64 ghcr.io/vmlabdev/vmlab-templates/<name>:<version>`.
  The older versions are still there.

Everything else stays under `~/win3d-zeus/` on Zeus ([Cleanup](#cleanup)).

## Lab files and scripts (`labs/zeus/`)

> **Update: vmlab 0.25 (2026-10-17).** The lab files below now use the
> vmlab 0.25.0-alpha form, `gpu { mode = "vulkan" host_gpu = "<name>" }`,
> which 0.24 does not accept (`docs/host.md`, "Update: vmlab 0.25"). Every run
> on this page was taken on vmlab 0.24 with the earlier form,
> `gpu { mode = "vulkan" address = "<PCI address>" }`; each evidence
> directory keeps that copy as `lab-vmlab.wcl`. Zeus was not changed. Before
> the next run on Zeus, upgrade it to vmlab 0.25.0-alpha or later and write
> its host config, `~/.config/vmlab/config.wcl`:
>
> ```wcl
> import <vmlab-host.wcl>
>
> gpu "Intel1"  { address = "0000:00:02.0" }                  // Iris Xe (ANV, renderD129)
> gpu "Nvidia1" { address = "0000:01:00.0"  default = true }  // RTX 4060 Max-Q (renderD128)
> ```
>
> `labs/zeus/README.md` repeats this. `labs/zeus/vmlab-iso.sh` changes only
> the runtime and state directories, so its isolated supervisor reads the
> same host config.

| File | What it is |
| --- | --- |
| `intel/vmlab.wcl` | `win3d-zeus-intel`: `gpu { mode = "vulkan" host_gpu = "Intel1" }` (`0000:00:02.0` in Zeus's host config), `secure_boot = false`, `agent_update = false`. vmlab passes `virtio-vga-gl,venus=true,blob=true,hostmem=4G`, `-display egl-headless,rendernode=/dev/dri/renderD129` and `VK_LOADER_DRIVERS_SELECT=*intel*` (`intel/*/qemu-argv-env.txt`) |
| `nvidia/vmlab.wcl` | `win3d-zeus-nvidia`, the same with `host_gpu = "Nvidia1"` (`0000:01:00.0`, `renderD128`, `*nvidia*`) |
| `amd/vmlab.wcl` | `win3d-zeus-amd`: `labs/probe/vmlab.wcl` with its own lab name (`host_gpu = "AMD1"`), for the regression run on helios |
| `stage.sh` | Copies what a run needs from helios to `~/win3d-zeus/repo` on Zeus: `out/package`, `out/probes`, `tests/guest`, `tests/e2e` from `main`, `scripts/install-into-vm.sh`, `labs/zeus`, `patches/host`, and the uavtest build |
| `run.sh` | `tests/e2e/run.sh` adapted for Zeus. It brings up a fresh lab, installs with `scripts/install-into-vm.sh` (the documented command), then runs vulkaninfo, the 10 probes with `vmlab vm screenshot` captures, dxdiag and uavtest. `tests/e2e/check.py` judges the result. It does not build: the package comes from helios |
| `vmlab-iso.sh` | Runs vmlab against an isolated supervisor under `~/win3d-zeus/`, whose `PATH` and environment give QEMU a patched QEMU (`patches/host/build-qemu.sh`, built on Zeus) or the diagnostic render server. helios used `labs/nvidia/vmlab-nv.sh` the same way until vmlab 0.26's `qemu_binary` replaced it ([`nvidia.md`](nvidia.md)) |
| `uavtest/` | A D3D11 FL11_0 compute test for typed UAV loads: it prints the caps and checks R32_FLOAT and R32_UINT read-modify-write. `uavtest/build.sh` builds it in the toolchain image |

## Commands

On helios:

```sh
scripts/build.sh                   # the package, from this branch
labs/zeus/uavtest/build.sh         # out/labs-zeus/uavtest/uavtest.exe
labs/zeus/stage.sh                 # to zeus:~/win3d-zeus/repo
```

On Zeus, in `~/win3d-zeus/repo` (vmlab 0.25.0-alpha or later and the host
config above; the recorded runs used vmlab 0.24 and the `address` form):

```sh
labs/zeus/run.sh --gpu intel                       # user's supervisor, host QEMU
patches/host/build-qemu.sh                         # patched QEMU, for NVIDIA
labs/zeus/run.sh --gpu nvidia                      # through labs/zeus/vmlab-iso.sh
```

The Intel diagnostic run used the isolated supervisor with the host QEMU and
the diagnostic render server (`patches/host/build-virglrenderer.sh`, built
on helios):

```sh
WIN3D_ISO=1 WIN3D_QEMU_DIR=host \
WIN3D_RENDER_SERVER=$PWD/out/host/virglrenderer/virgl_render_server \
  labs/zeus/run.sh --gpu intel --keep
```

The AMD regression on helios, from this worktree:

```sh
git archive main tests/e2e | tar -C .scratch/e2e-main -xf -
WIN3D_WORK=$PWD/.scratch/work WIN3D_E2E=$PWD/.scratch/e2e-main/tests/e2e \
  labs/zeus/run.sh --gpu amd
```

`run.sh` also takes `WIN3D_BUNDLE=<dir>`, which installs another bundle, and
`WIN3D_TAG=<name>`, which writes the evidence to
`docs/evidence/zeus/<gpu>/<name>/`.

## Intel Iris Xe

### Pass/fail

`main` = the package at `db07da3`. `0021` = this branch's package with
`patches/mesa/0021` (manifest commit `1bbbfe8`, the final version of the
patch).

The `main` column comes from the diagnostic run: the isolated supervisor
with the host QEMU and the diagnostic render server, which only adds log
lines. The plain run before it, on the user's supervisor with the stock
render server, failed the same way, with no logon. I stopped it once the
dwm crash loop was clear, and the next run overwrote its evidence.

| Check | `main` (`intel/02-diag/`) | `0021` (`intel/05-fix-0021/`) |
| --- | --- | --- |
| Install (`scripts/install-into-vm.sh`) | pass | pass |
| vulkaninfo lists Venus (x64, x86) | pass: `Virtio-GPU Venus (Intel(R) Iris(R) Xe Graphics (RPL-P))` | pass: `Virtio-GPU Venus (Intel(R) Iris(R) Xe Graphics (RPL-P))` |
| D3D9 x64 / x86 | FAIL: renders (readback ok), but no window on the console | pass: 59.3 / 58.0 fps, readback ok, window on the console, animating |
| D3D11 x64 / x86 | FAIL: `D3D11CreateDevice(HARDWARE)` `0x80004005` | pass: 59.8 / 59.8 fps |
| D3D12 x64 / x86 | FAIL: no window on the console | pass: 56.9 / 56.9 fps |
| OpenGL x64 / x86 | FAIL: `SwapBuffers` fails | pass: Zink on Venus (ANV), 61.2 / 62.5 fps |
| Vulkan x64 / x86 | FAIL: `vkCreateSwapchainKHR` `VK_ERROR_INITIALIZATION_FAILED` | pass: 58.4 / 59.3 fps |
| dxdiag hardware acceleration | pass (D3D, DDraw and AGP Enabled) | pass (D3D, DDraw and AGP Enabled, WDDM 2.0) |
| Console screenshots | black (`console-desktop.png`) | pass: `console-*.png`; each probe window is in 8 to 10 of 12 to 14 frames and animates (`shots.json`) |
| uavtest (R32 typed UAV loads) | not run | pass: R32_FLOAT and R32_UINT read-modify-write correct; `TypedUAVLoadAdditionalFormats: 0`; `UAV_TYPED_LOAD` on R32 only (`uavtest.txt`) |
| Done checks (`results.txt`) | 4 of 12 | **12 of 12** |

`intel/03-experiment/` is the experiment that proved the cause: the `main`
package with the failing check turned into a log line and nothing else. It
passed 11 of 12. Its D3D12 x86 probe presented one frame in 73 seconds and
showed no window. Run again in the same lab, the probe presented 795 frames
at 39.7 fps and showed on the console (`03-experiment/rerun-x86-d3d12/`).
The first attempt ran while the laptop's load average was about 35.

### Cause: a Vulkan feature the D3D11 UMD made mandatory

After the install's reboot, dwm crashes in `dwmcore.dll` with `0x889800b0`
and restarts in a loop (`02-diag/guest-dwm-crashes.txt`). In the first run,
under the user's supervisor and the stock render server, nobody got logged
on. In the diagnostic run, explorer started, but every D3D11 device failed,
and so did the Vulkan and OpenGL presents. Those present through the system
DXGI and D3D11 on the same UMD. The render server logged no error at all
(`02-diag/qemu-log.txt`).

The Yttrium UMD's log (`C:\ProgramData\Yttrium\yttrium-user.log`) has one
line, 722 times, from explorer, StartMenuExperienceHost, the probes and
dxdiag:

```
d3d10umd: WDDM 2.0 requires typed image loads
```

`CreateDevice` in `src/gallium/frontends/d3d10umd/Device.cpp` returns
`E_FAIL` when `SupportsTypedLoadsWDDM2_0()` is false. That function wants
formatless storage-image reads (typed UAV loads) for 15 formats, such as
R8G8B8A8_UNORM and R16G16B16A16_FLOAT. In Vulkan they need
`shaderStorageImageReadWithoutFormat`, and the host's ANV reports it as
false on this GPU:

| Host device | `shaderStorageImageReadWithoutFormat` |
| --- | --- |
| Intel Iris Xe (RPL-P), Mesa 26.2.2 ANV (Zeus) | **false** |
| AMD Raphael, RADV (helios) | true |
| NVIDIA RTX 4060 Laptop GPU, 615.71.09 (Zeus) | true |

D3D11 does not require those 15 formats. At feature level 11_0 only
R32_FLOAT, R32_UINT and R32_SINT typed UAV loads are mandatory. The others
are an optional group, reported as
`D3D11_FEATURE_DATA_D3D11_OPTIONS2::TypedUAVLoadAdditionalFormats`. The UMD
treated an optional capability as a reason to refuse the device. dwm cannot
start without a D3D11 device, so nothing composes the desktop and the
console stays black. D3D9 and D3D12 rendered (their readbacks were right),
but with no compositor, nothing they drew reached the scanout.

The D3D9, OpenGL (Zink) and Vulkan paths have no hard requirement like this
on ANV. With only this check relaxed, all of them pass
(`03-experiment/results.txt`), and the UMD log has no other rejection.

### The fix: `patches/mesa/0021`

`0021-yttrium-d3d11-typed-uav-loads-without-formatless-read.patch` (Mesa,
the Yttrium d3d10umd frontend and gdi winsys):

- `CreateDevice` no longer fails. It logs once that typed UAV loads are
  limited to R32 formats.
- `GetCaps(D3D11DDICAPS_SHADER)` sets
  `TYPED_UAV_LOAD_ADDITIONAL_FORMATS` only when the device supports the
  additional formats. The D3D11 runtime asks GetCaps before any device
  exists, so GetCaps asks the host over a short-lived Venus connection, once
  per process, as `D3D9QueryMsaaTypes` already does for D3D9. If it cannot
  connect (no display), it stays optimistic, as before. A first version
  used only the answer from the process's last device. It reported the cap
  on, wrongly, for every process's first device
  (`intel/04-fix-0021-v1/uavtest.txt`). `CheckFormatSupport` is per device
  and always exact.
- `CheckFormatSupport` reports `UAV_READS` for R32_FLOAT, R32_UINT and
  R32_SINT when the device can store to them, even without formatless reads.
- The shader pass that lowers formatless images declares a **loaded**
  storage image as R32_FLOAT, R32_UINT or R32_SINT, by its numeric type,
  when the device cannot read formatless images. Such a declaration needs no
  Vulkan feature. D3D allows loads only from R32 views in that case, so
  stores through the same image stay correct. The pipeline rejects a bound
  view whose format is not that R32 format, with a log line, instead of
  reading through a mismatched declaration.

On devices with `shaderStorageImageReadWithoutFormat` (RADV, NVIDIA),
nothing changes, apart from the GetCaps answer now coming from the same
check.

### Proof

| Run | Evidence | Result |
| --- | --- | --- |
| Intel, `0021` | `intel/05-fix-0021/` | 12 of 12 Done checks; uavtest passes and reports the additional formats off (above) |
| Intel, first version of `0021` | `intel/04-fix-0021-v1/` | 12 of 12; uavtest passes, but reports `TypedUAVLoadAdditionalFormats: 1` |
| AMD on helios, `0021`, `labs/zeus/amd/vmlab.wcl` (the `labs/probe/vmlab.wcl` form on `0000:72:00.0`, vmlab 0.24) | `amd/0021-v2/` | 12 of 12 Done checks; all 10 probes render, show on the console and animate; vulkaninfo lists Venus (RADV); dxdiag reports acceleration; uavtest passes with `TypedUAVLoadAdditionalFormats: 1` and `UAV_TYPED_LOAD` on R8G8B8A8_UNORM and R16G16B16A16_FLOAT, as before the patch |
| AMD on helios, first version of `0021` | `amd/0021-v1/` | 12 of 12 (uavtest did not run: a `run.sh` bug, since fixed) |

## NVIDIA RTX 4060

The run used `main`'s package, which includes `patches/mesa/0020`, and
QEMU 11.1.1 with `patches/host/qemu/0001`. That QEMU was built natively on
Zeus with `patches/host/build-qemu.sh` into `~/win3d-zeus/repo/out/host/qemu`
and run through `labs/zeus/vmlab-iso.sh` (`nvidia/host.txt`).

| Check | Result (`nvidia/`) |
| --- | --- |
| Install | pass |
| Desktop after the install, on the console | pass: dwm and explorer run, and the console shows the desktop in Test Mode (`console-desktop.png`, 19:18) |
| vulkaninfo, the 10 probes, dxdiag | **not run**: QEMU died during vulkaninfo |

### What happened

The kernel log (`nvidia/kernel-nvrm.txt`):

```
19:19:13 NVRM: GPU0 nvAssertFailedNoLog: Assertion failed: Failed to boot GSP @ kernel_gsp_falcon_tu102.c:495
19:19:58 NVRM: Xid (PCI:0000:01:00): 119, Timeout after 46s of waiting for RPC response from GPU0 GSP!
19:20:00 NVRM: Xid (PCI:0000:01:00): 79, pid=3628778, name=vkr-ring-5, GPU has fallen off the bus.
19:20:00 NVRM: Xid (PCI:0000:01:00): 154, GPU recovery action changed from 0x0 (None) to 0x1 (PF FLR)
```

`vkr-ring-5` is a Venus ring thread of the render server. The render
server then failed a lookup, `failed to look up object 1652 of type 8`
(`VkDeviceMemory`), and `vkBindImageMemory resulted in CS error`
(`nvidia/qemu-log.txt`). QEMU aborted inside `libnvidia-eglcore.so.615.71.09`
(`nvidia/qemu-coredump.txt`). Afterwards `nvidia-smi` reports
`No devices were found` (`nvidia-smi-after.txt`), and the device's runtime
power state is `error`. Only a function-level reset (root) or a reboot
brings the GPU back. I used no sudo and did not retry.

The kernel log has no other GSP failure or Xid in the two days before this
run. The trigger is the GSP failing to boot while the guest's Venus
workload was running. What made it fail is unknown: it could be the
driver's runtime power management, the laptop's power state, or the
workload itself. The evidence here cannot separate them. Before any
further NVIDIA run on Zeus, Zeus needs a reboot, and the run should be
watched with the user's consent.

## Cleanup

On Zeus, every lab I created is destroyed: `win3d-zeus-intel` and
`win3d-zeus-nvidia`. The isolated supervisor is stopped. `~/win3d-zeus/` is
removed, with the staging tree, the patched QEMU build, the diagnostic
logs and a copy of the vmlab 0.21.2 binary. So are my lab state
directories under `~/.local/state/vmlab/labs/` and
`/run/user/1000/vmlab/labs/`. I also stopped a stray supervisor I had
started by mistake (a `vmlab vm screenshot` without `XDG_RUNTIME_DIR`,
under `/tmp/vmlab-1000`) and removed its directory.

These stay, on purpose or because they are not mine to remove:

- vmlab 0.24.0-alpha in `~/.local/bin/vmlab`, the 0.24 guest assets, and
  the pulled templates `windows-11@26100.1742.5` and
  `windows-10@19045.2006.8` (the authorised upgrade). To go back, run
  `curl -fsSL https://vmlab.io/install.sh | sh -s -- --version v0.21.2-alpha`.
- The user's supervisor, still the one I restarted (pid 2419267). It
  started before the user's `setcap`, so it gets the afxdp fast path back
  only after `vmlab daemon stop` and `vmlab daemon start`, or the reboot.
- A systemd coredump of the aborted QEMU (pid 3445680), which is
  system-owned.
- The RTX 4060 failure. Zeus needs a reboot to get the GPU back.
