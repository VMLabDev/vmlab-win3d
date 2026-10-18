# Zeus: the package on an Intel iGPU and a laptop RTX 4060

This page records how the package behaves on Zeus, the user's laptop, on
its two GPUs. It gives the host, the lab files, the exact commands, a
pass/fail table per GPU, and the cause of each failure. Every claim cites a
file under `docs/evidence/zeus/`. The first runs are from 2026-10-17; the
second round, on vmlab 0.28 and the package from `main`, is from 2026-10-19.

## Result

Newest first. The package rows name the commit in the bundle's
`manifest.json`; `82d8e51` and `2c3d8cd` differ only in `installer/VERSION`
(0.1.0 and 0.1.1).

| Date | GPU | Package, host | Result |
| --- | --- | --- | --- |
| 2026-10-19 | Intel Iris Xe (Raptor Lake-P), ANV | `main` (`82d8e51`, patches 0001 to 0042), vmlab 0.28 | **Passes**, 12 of 12 Done checks; uavtest passes (`intel/06-main-82d8e51/`) |
| 2026-10-19 | NVIDIA RTX 4060 Laptop, 615.71.09 | `main` (`82d8e51`) + the patched QEMU `d8a6048aae`, vmlab 0.28 | **Fails, 7 of 12.** The install, vulkaninfo, D3D9 and D3D11 pass. Every client that drives Venus through the Vulkan API (the Vulkan probes, D3D12 through vkd3d-proton, OpenGL through Zink) never sees its first fence signal, and dxdiag, run after them, returns nothing. The GPU stays healthy: no Xid in any run, with or without the keep-awake helper. Cause below |
| 2026-10-19 | NVIDIA RTX 4060 Laptop | the same, with Venus feedback turned off in the guest (`VN_PERF`) | **11 of 12, twice.** Everything passes except one OpenGL probe per run, whose final readback is black although it rendered and showed on the console |
| 2026-10-17 | Intel Iris Xe (Raptor Lake-P), ANV | `main` (`db07da3`), vmlab 0.24 | **Fails.** dwm crash-loops after the install, the console is black, and no probe shows. Cause below |
| 2026-10-17 | Intel Iris Xe (Raptor Lake-P), ANV | with `patches/mesa/0021` | **Passes**, 12 of 12 Done checks: vulkaninfo, all 10 probes on the console, dxdiag. R32 typed UAV loads work (uavtest). Fix below |
| 2026-10-17 | AMD iGPU on helios, RADV (regression) | with `patches/mesa/0021` | **Passes**, 12 of 12 Done checks, 10 of 10 probes |
| 2026-10-17 | NVIDIA RTX 4060 Max-Q, 615.71.09 | `main` (`db07da3`) + patched QEMU 11.1.1 | **Stopped.** Install, desktop and console work. One minute later the GPU **fell off the bus** (Xid 79), and QEMU aborted. The GPU stayed unusable until Zeus rebooted |

## Host

| Item | Value |
| --- | --- |
| Machine | Zeus, a laptop the user works on; i7-13700H (x86-64-v3, no AVX-512), 20 threads, 93 GiB |
| OS | CachyOS (x86-64-v3 repositories), kernel 7.2.4-3-cachyos |
| GPUs | Intel Iris Xe (Raptor Lake-P) `0000:00:02.0`, `renderD129`; NVIDIA RTX 4060 Max-Q / Laptop (AD107M) `0000:01:00.0`, `renderD128`, driver 615.71.09 (open kernel module), runtime D3 fine-grained (`DynamicPowerManagement=2`) |
| Host stack | QEMU 11.1.1, virglrenderer 1.3.0, Mesa 26.2.2 (`vulkan-intel`), the same as helios |
| Venus QEMU on `Nvidia1` | 2026-10-19: VMLabDev/qemu `d8a6048aae` (upstream master + our egl-headless series, `patches/host/PINS`), built on Zeus, in `~/.local/opt/qemu-vmlab-egl` (`docs/evidence/zeus/qemu/`). 2026-10-17: QEMU 11.1.1 + `patches/host/qemu/0001` through `labs/zeus/vmlab-iso.sh` |
| vmlab | 2026-10-19: 0.28.0-alpha, with the host config below. 2026-10-17: 0.24.0-alpha, no host config |
| Template | `x86_64/windows-11@26100.1742.5` |

`host.txt` in each evidence directory records these per run.

Zeus's host config, `~/.config/vmlab/config.wcl` (copy in
`docs/evidence/zeus/qemu/zeus-config.wcl`):

```wcl
import <vmlab-host.wcl>

// Host GPUs on zeus. A lab's `gpu {}` names one with `host_gpu`, or takes the default.
gpu "Intel1"  { address = "0000:00:02.0" }                  // Iris Xe RPL-P (i915/ANV, renderD129)
gpu "Nvidia1" {                                             // RTX 4060 Laptop GPU (nvidia, renderD128)
  address     = "0000:01:00.0"
  default     = true
  // QEMU (upstream master + our egl-headless external-only dma-buf series)
  // (VMLabDev/qemu vmlab/egl-headless-external-dmabuf, d8a6048aae); stock
  // QEMU shows a black console for a Venus guest on NVIDIA.
  qemu_binary = "/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
}
```

### What changed on Zeus

The user authorised these changes.

On 2026-10-19:

- vmlab went from 0.24.0-alpha to 0.28.0-alpha, the version helios runs:
  `curl -fsSL https://vmlab.io/install.sh | sh -s -- --version v0.28.0-alpha --no-fastpath`.
  This also installed the 0.28 guest assets. The installer replaces the
  binary, so its file capabilities are gone: **the user has to run
  `sudo setcap cap_bpf,cap_net_admin+ep ~/.local/bin/vmlab` again** and then
  restart the supervisor (`vmlab daemon stop`, `vmlab daemon start`). Until
  then the network uses the userspace fast path (`vmlab fastpath`). No
  supervisor was running when the upgrade was made (Zeus had rebooted); I
  started the 0.28 one with `vmlab daemon start`.
- The host config above is new; Zeus had none.
- The patched QEMU is installed in `~/.local/opt/qemu-vmlab-egl`, from a
  tarball laid out as `scripts/release.sh` lays it out, with its
  `install.sh --check` and `install.sh` (`docs/evidence/zeus/qemu/install-zeus-native.txt`).
  The binary was built on Zeus with `patches/host/build-qemu.sh`, because
  the one built on helios does not run on Zeus (next section).

On 2026-10-17:

- vmlab was upgraded with the official installer,
  `curl -fsSL https://vmlab.io/install.sh | sh -s -- --version v0.24.0-alpha --no-fastpath`,
  and the user's supervisor was restarted. The user then ran
  `sudo setcap cap_bpf,cap_net_admin+ep ~/.local/bin/vmlab` themselves.
- The templates `windows-11@26100.1742.5` and `windows-10@19045.2006.8` were
  pulled with `vmlab template pull --arch x86_64 ghcr.io/vmlabdev/vmlab-templates/<name>:<version>`.
  The older versions are still there.

### A QEMU built on helios does not run on Zeus

The release tooling builds the QEMU tarball on helios. helios is a Zen 5
host on CachyOS's znver4 repositories, and its build of `d8a6048aae` is
marked `x86 ISA needed: ... x86-64-v4`. Zeus's i7-13700H has no AVX-512
(x86-64-v3), so the dynamic loader refuses the binary with `CPU ISA level is
lower than required`. `install.sh --check` passed anyway, because it checks
only the shared libraries with `ldd`
(`docs/evidence/zeus/qemu/helios-binary-on-zeus.txt`). The same commit built
on Zeus is marked x86-64-v3 and runs
(`docs/evidence/zeus/qemu/BUILDINFO-zeus-native.txt`). A release tarball built
on helios therefore needs an AVX-512 host. This is reported to the release
tooling's owner; `scripts/release` is not part of this page's slice.

## Lab files and scripts (`labs/zeus/`)

The lab files use the vmlab 0.25+ form, `gpu { mode = "vulkan" host_gpu =
"<name>" }`, resolved through Zeus's host config (above). The 2026-10-17 runs
used vmlab 0.24 and the earlier form, `gpu { mode = "vulkan" address = "<PCI
address>" }`; each evidence directory keeps its copy as `lab-vmlab.wcl`.

| File | What it is |
| --- | --- |
| `intel/vmlab.wcl` | `win3d-zeus-intel`: `gpu { mode = "vulkan" host_gpu = "Intel1" }` (`0000:00:02.0` in Zeus's host config), `secure_boot = false`, `agent_update = false`. vmlab passes `virtio-vga-gl,venus=true,blob=true,hostmem=4G`, `-display egl-headless,rendernode=/dev/dri/renderD129` and `VK_LOADER_DRIVERS_SELECT=*intel*` (`intel/*/qemu-argv-env.txt`) |
| `nvidia/vmlab.wcl` | `win3d-zeus-nvidia`, the same with `host_gpu = "Nvidia1"` (`0000:01:00.0`, `renderD128`, `*nvidia*`). vmlab runs the host config's `qemu_binary` for it. Both Zeus labs have `nic { nat = true }` since 2026-10-19 |
| `amd/vmlab.wcl` | `win3d-zeus-amd`: `labs/probe/vmlab.wcl` with its own lab name (`host_gpu = "AMD1"`), for the regression run on helios |
| `stage.sh` | Copies what a run needs from helios to `~/win3d-zeus/repo` on Zeus: `out/package`, `out/probes`, `tests/guest`, `tests/e2e` from `main`, `scripts/install-into-vm.sh`, `labs/zeus`, `patches/host`, and the uavtest build |
| `run.sh` | `tests/e2e/run.sh` adapted for Zeus. It brings up a fresh lab, installs with `scripts/install-into-vm.sh` (the documented command), then runs vulkaninfo, the 10 probes with `vmlab vm screenshot` captures, dxdiag and uavtest. `tests/e2e/check.py` judges the result. It does not build: the package comes from helios. On `--gpu nvidia` it records the kernel's NVRM lines (`kernel-nvrm.txt`) and every runtime power state change of the GPU (`runtime-pm.txt`), and stops the run on an Xid 79 after saving `nvidia-smi` and the power state (`gpu-lost.txt`). `WIN3D_KEEPAWAKE=1` runs `keepawake/` for the whole run, `WIN3D_SMOKE=1` runs only vulkaninfo and the x64 D3D11 probe after the install, and `WIN3D_GUEST_ENV="NAME=VALUE ..."` sets machine-wide guest environment variables before the install |
| `keepawake/` | `keepawake.c`: holds an OpenGL ES context on one GPU through EGL's device platform and clears a 16x16 renderbuffer every 100 ms, so NVIDIA's runtime D3 never suspends the GPU; no root. `build.sh` builds it on the host; `pm-check.sh` samples `runtime_status` with no client, with a context only (`--hold-only`) and with the clears |
| `probe-order.sh` | In a lab `run.sh --keep` left, restarts the VM and runs single probes in a given order |
| `envprobe.sh` | In the same kept lab, runs single probes with `VN_PERF` set for the probe process only |
| `vmlab-iso.sh` | Runs vmlab against an isolated supervisor under `~/win3d-zeus/`, whose `PATH` and environment give QEMU a patched QEMU or the diagnostic render server (`WIN3D_ISO=1`). The 2026-10-17 NVIDIA run used it; vmlab 0.26's `qemu_binary` replaced it ([`nvidia.md`](nvidia.md)) |
| `uavtest/` | A D3D11 FL11_0 compute test for typed UAV loads: it prints the caps and checks R32_FLOAT and R32_UINT read-modify-write. `uavtest/build.sh` builds it in the toolchain image |

## Commands

On helios:

```sh
scripts/build.sh                   # the package, from this branch
labs/zeus/uavtest/build.sh         # out/labs-zeus/uavtest/uavtest.exe
labs/zeus/stage.sh                 # to zeus:~/win3d-zeus/repo
```

On Zeus, in `~/win3d-zeus/repo` (vmlab 0.25.0-alpha or later and the host
config above), as on 2026-10-19:

```sh
labs/zeus/run.sh --gpu intel                                    # Intel1
WIN3D_SMOKE=1 labs/zeus/run.sh --gpu nvidia                     # 4060 smoke test
labs/zeus/run.sh --gpu nvidia                                   # run A: runtime PM as is
WIN3D_KEEPAWAKE=1 labs/zeus/run.sh --gpu nvidia --keep          # run B: keep-awake
labs/zeus/probe-order.sh nvidia <out> x64-vulkan x64-d3d11 x64-d3d12 x64-vulkan x64-d3d11
labs/zeus/envprobe.sh <out> nofb no_fence_feedback,no_semaphore_feedback,no_event_feedback,no_query_feedback x64-vulkan x64-d3d12
WIN3D_GUEST_ENV=VN_PERF=no_fence_feedback,no_semaphore_feedback,no_event_feedback,no_query_feedback \
  labs/zeus/run.sh --gpu nvidia                                 # runs C and C2
labs/zeus/keepawake/build.sh && labs/zeus/keepawake/pm-check.sh
```

Each run took `WIN3D_TAG=<name>`, so its evidence went to
`docs/evidence/zeus/<gpu>/<name>/`, fetched with
`labs/zeus/stage.sh --fetch <gpu>/<name>`.

The patched QEMU was built on Zeus and installed through the release
layout (`scripts/release.sh`, stage "QEMU tarball"):

```sh
patches/host/build-qemu.sh                  # out/host/qemu, the PINS commit
# bin/, BUILDINFO, licenses/, install.sh (scripts/release/qemu-install.sh), README.txt
./install.sh --check && ./install.sh        # ~/.local/opt/qemu-vmlab-egl
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

### Regression on `main` (2026-10-19)

The package from `main` at `82d8e51`, which carries `patches/mesa/0021` and
every later patch up to `0042`, passes 12 of 12 Done checks on vmlab 0.28
(`intel/06-main-82d8e51/results.txt`). All 10 probes render, read back
correctly and animate on the console at 55 to 63 fps; vulkaninfo lists
`Virtio-GPU Venus (Intel(R) Iris(R) Xe Graphics (RPL-P))`; dxdiag reports
D3D, DDraw and AGP acceleration. uavtest passes with
`TypedUAVLoadAdditionalFormats: 0`, as with `0021` alone (`uavtest.txt`).

### Pass/fail (2026-10-17)

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

### 2026-10-19: what was run

Zeus had been rebooted, and the 4060 was healthy (`nvidia-smi -L` listed
it, no Xid in the boot's kernel log). Every run used the package from `main`
and the patched QEMU `d8a6048aae` through `qemu_binary`, the setup that
passes 12 of 12 on helios's RTX 5090s (`docs/known-limits.md`). `run.sh`
watched the kernel's NVRM lines and the GPU's runtime power state for the
whole of each run.

| Run | Evidence (`nvidia/`) | Runtime PM | GPU suspend/resume cycles | NVRM or Xid lines | Done checks |
| --- | --- | --- | --- | --- | --- |
| Smoke: install, vulkaninfo, x64 D3D11 probe | `10-smoke/` | `auto` | 9 | none | vulkaninfo and D3D11 pass (58.9 fps, on the console); the rest not run |
| A: full acceptance | `11-a-pm-auto/` | `auto` | 25 | none | **7 of 12** |
| B: full acceptance, `keepawake` running | `12-b-keepawake/` | `auto`, held `active` | **0** | none | **7 of 12**, the same checks as A |
| Probe order, after a VM restart | `13-isolate/order/` | `auto` | 3 | none | (single probes, below) |
| C: full acceptance, guest `VN_PERF` | `14-c-vn-perf/` | `auto` | 8 | none | **11 of 12** |
| C2: C again | `15-c2-vn-perf/` | `auto` | 7 | none | **11 of 12** |

### Runtime power management on the 4060

The NVIDIA driver runs this GPU with fine-grained runtime D3
(`/proc/driver/nvidia/gpus/0000:01:00.0/power`: `Runtime D3 status: Enabled
(fine-grained)`; `DynamicPowerManagement: 2`), and `power/control` is `auto`.
When the GPU has had no work for a few seconds, the driver powers it down,
whether or not a process holds it open. Its first use afterwards powers it
up again, and that includes booting the GSP firmware. `pm-check.sh` shows
it (`nvidia/pm/pm-check.txt`):

| Phase (40 one-second samples) | `runtime_status` |
| --- | --- |
| no client | active for 21 s after the previous use, then suspended |
| `keepawake --hold-only`: an EGL context, no work | resuming, 21 s active, then **suspended** while the context is still open |
| `keepawake`: a 16x16 clear every 100 ms | **active** throughout (39 active, 1 resuming) |
| no client again | suspended 7 s after `keepawake` stops |

A Venus guest is such a client. QEMU's render server holds Vulkan devices
on the GPU, but whenever the guest is idle for a few seconds (between
probes, at the desktop, during reboots) the GPU suspends, and the next frame
resumes it. Run A went through 25 suspend/resume cycles, and all the
2026-10-19 runs together through 52. Not one logged an NVRM error or an Xid.
`keepawake` held the GPU `active` for all of run B (15,853 clears,
`12-b-keepawake/keepawake.log`, one `runtime-pm.txt` line), and B failed
exactly as A did.

### Cause of the 7 of 12: Venus feedback never reaches the guest

In A and B, the install, vulkaninfo, D3D9 and D3D11 pass. The D3D12 probes
fail with `GPU fence 1 not reached within 10 s (device removed reason
0x00000000)`, the OpenGL probes time out, the Vulkan probes fail with
`vkWaitForFences failed: VK_ERROR_UNKNOWN (-13)`, and dxdiag, run after them,
returns nothing (`11-a-pm-auto/results.txt`). The host shows no fault:

- no NVRM line, and the GPU went on suspending and resuming normally after
  the hang (`11-a-pm-auto/runtime-pm.txt`);
- QEMU logged only two `virtio_gpu_virgl_process_cmd: ctrl 0x102, error
  0x1203` lines (`qemu-log.txt`);
- during the hang every render server process sat idle in `futex_wait`,
  and the guest kept opening new Venus contexts
  (`12-b-keepawake/host-threads-during-hang.txt`).

Single probes after a VM restart separate it (`13-isolate/order/order.txt`):

| Order | Probe | Result |
| --- | --- | --- |
| 1 | x64 Vulkan | FAIL: `VK_ERROR_UNKNOWN` from `vkWaitForFences` |
| 2 | x64 D3D11 | pass, 57.9 fps |
| 3 | x64 D3D12 | FAIL: fence 1 not reached |
| 4 | x64 Vulkan | FAIL, the same |
| 5 | x64 D3D11 | pass, 57.8 fps |

So nothing breaks the device. Every client that uses Vulkan fences or
timeline semaphores fails from its first wait, and the Yttrium D3D9 and D3D11
driver, which does not wait that way, works throughout. vulkaninfo never
waits on a fence.

`VK_ERROR_UNKNOWN` points at the mechanism. Mesa's Venus driver keeps a
*feedback* slot for each fence, timeline semaphore, event and query: a word
in host-visible memory that a command the driver appends to the submission
writes on the host GPU, and that the guest reads to learn the status
without a round trip. In `vn_get_fence_status`
(`src/virtio/vulkan/vn_queue.c`, Mesa at `build/mesa/PINS`), the only place
that returns `VK_ERROR_UNKNOWN` is the case "the renderer reports the fence signalled, but the
feedback slot is not" (`ERROR: ffb must be signaled now`). The host GPU
finishes the work, but its write to the feedback memory never becomes
visible to the guest.

Turning the feedback off with Venus's `VN_PERF` options, for one probe
process at a time, confirms it (`13-isolate/vn-perf/env.txt`):

| `VN_PERF` | x64 Vulkan | x64 D3D12 | x64 OpenGL |
| --- | --- | --- | --- |
| (unset) | FAIL | FAIL | FAIL (timeout) |
| `no_fence_feedback` | **pass**, 57.0 fps | FAIL (fence 2) | not run |
| `no_semaphore_feedback` | FAIL | FAIL | not run |
| `no_fence_feedback,no_semaphore_feedback` | not run | **pass**, 50.3 fps | readback black |
| `no_fence_feedback,no_semaphore_feedback,no_event_feedback,no_query_feedback` | **pass**, 57.0 fps | **pass**, 51.5 fps | **pass**, 57.7 fps |

The x86 Vulkan and D3D12 probes pass with all four too. Why the writes do
not arrive is not proven. The guest sees the host's five memory types
unchanged (`13-isolate/vn-perf/guest-vulkaninfo-x64.txt`). Venus puts the
feedback buffer in the first host-coherent type the buffer allows, which on
both this host and the 5090 is most likely type 2 (system memory); this was
not checked. Two things differ from helios's RTX 5090s, where the
same package and QEMU pass: the driver (615.71.09 here, 610.57.04 there) and
the platform (a laptop with the whole 8 GiB of VRAM host-visible through a
resizable BAR, where the 5090 exposes a 246 MiB window;
`13-isolate/host-vulkaninfo-*.txt`). Telling them apart needs either driver
on the other GPU, which decision D3 (no host system updates) rules out here.

### With the feedback off: 11 of 12

Runs C and C2 set the four options machine-wide in the guest before the
install, `WIN3D_GUEST_ENV=VN_PERF=no_fence_feedback,no_semaphore_feedback,no_event_feedback,no_query_feedback`
(`14-c-vn-perf/guest-env.txt`). Both passed 11 of 12: vulkaninfo, D3D9, D3D11,
D3D12 (51 to 54 fps), Vulkan and dxdiag, all on the console. In each run
one OpenGL probe failed, x86 in C and x64 in C2: it rendered over 1,000
frames at 54 to 56 fps and showed and animated on the console, but its
final `glReadPixels` returned black at all three marker pixels. Repeated in
C2's kept lab, the OpenGL readback failed 3 times in 6 with the four
options, and 1 in 6 with `no_async_queue_submit,no_cmd_batching,no_multi_ring`
added (`15-c2-vn-perf/gl-repeats/env.txt`). That looks like the same fault
in another form: data the host GPU writes to host-visible memory that the
guest then reads too early or not at all. It is not solved.

`VN_PERF` is a diagnostic switch, not a fix: it costs a round trip to the
host for every fence and semaphore status check, and the package does not
set it. A fix belongs in the package's Mesa (or a host-side cause has to be
found), and it is left for a follow-up.

### Runtime power management: conclusion and recommendation

The power theory for the 2026-10-17 failure is neither confirmed nor
excluded. That failure began while the GSP was booting, which on this
GPU happens at every runtime resume. On 2026-10-19 the GPU resumed 52 times
under Venus without a single error, and holding it awake changed no result.
One failure in the first run and none in the next 52 resumes give no
evidence that runtime D3 is the trigger, and the evidence cannot exclude a
rare resume failure either.

Recommendation: no host change is required, and `keepawake` stays a lab
tool in `labs/zeus/keepawake/`, not a shipped one. A user who wants to rule
runtime D3 out for a session can keep the GPU powered with one of these
root commands. They were not run here (no sudo), and both keep the dGPU
powered all the time, which costs battery on a laptop:

```sh
# Until the next reboot:
echo on | sudo tee /sys/bus/pci/devices/0000:01:00.0/power/control
# Back to the default:
echo auto | sudo tee /sys/bus/pci/devices/0000:01:00.0/power/control

# Permanently (NVIDIA driver option; reboot afterwards, and regenerate the
# initramfs if it loads nvidia, e.g. `sudo mkinitcpio -P` on Arch/CachyOS):
echo 'options nvidia NVreg_DynamicPowerManagement=0x00' | sudo tee /etc/modprobe.d/nvidia-no-rtd3.conf
```

`keepawake` does the same without root, for as long as it runs:
`labs/zeus/keepawake/build.sh && labs/zeus/keepawake/keepawake --pci 0000:01:00.0`.

### 2026-10-17: the GPU fell off the bus

The run used `main`'s package, which includes `patches/mesa/0020`, and
QEMU 11.1.1 with `patches/host/qemu/0001`. That QEMU was built natively on
Zeus with `patches/host/build-qemu.sh` into `~/win3d-zeus/repo/out/host/qemu`
and run through `labs/zeus/vmlab-iso.sh` (`nvidia/host.txt`).

| Check | Result (`nvidia/`) |
| --- | --- |
| Install | pass |
| Desktop after the install, on the console | pass: dwm and explorer run, and the console shows the desktop in Test Mode (`console-desktop.png`, 19:18) |
| vulkaninfo, the 10 probes, dxdiag | **not run**: QEMU died during vulkaninfo |


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
workload itself. The evidence here cannot separate them. The 2026-10-19
runs above tested the runtime power management theory.

## Cleanup

### 2026-10-19

On Zeus, both labs I created (`win3d-zeus-intel`, `win3d-zeus-nvidia`) are
destroyed, their state directories under `~/.local/state/vmlab/labs/` and
`/run/user/1000/vmlab/labs/` are removed, and so is `~/win3d-zeus/` (the
staging tree, the QEMU build tree, the keepawake build and the experiment
outputs, all copied into `docs/evidence/zeus/` first). No `keepawake` or
monitor process is left. The 4060 ended healthy: `nvidia-smi -L` lists it,
and the boot's kernel log has no Xid.

These stay, as the deployment:

- vmlab 0.28.0-alpha in `~/.local/bin/vmlab`, its guest assets, and the
  0.28 supervisor I started. The user still has to run the `setcap` above
  and restart the supervisor.
- `~/.config/vmlab/config.wcl`, the host config above.
- `~/.local/opt/qemu-vmlab-egl`, the patched QEMU that `Nvidia1`'s
  `qemu_binary` names.

### 2026-10-17

Every lab I created was destroyed, the isolated supervisor stopped, and
`~/win3d-zeus/` removed, with my lab state directories. I also stopped a
stray supervisor I had started by mistake (a `vmlab vm screenshot` without
`XDG_RUNTIME_DIR`, under `/tmp/vmlab-1000`) and removed its directory. A
systemd coredump of the aborted QEMU (pid 3445680) stayed, being
system-owned, and the RTX 4060 needed the user's reboot to come back.
