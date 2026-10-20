# Acceptance

This page checks the "Done" criteria in `docs/brief.md` one by one, on fresh
vmlab labs from the local `windows-11` and `windows-10` templates. One script
produces all of the evidence, and anyone can run it again:

```sh
tests/e2e/run.sh --template windows-11     # or windows-10; --keep leaves the lab up
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1   # another host GPU (default AMD1)
```

It exits 0 only when every Done check passes, and it prints a pass/fail
table. The tables below come from each run's `results.json`
(`tests/e2e/report.py`).

## What the script does

From nothing, with no manual step:

1. **Build.** Runs `scripts/build.sh`. If `out/package/` was built from a
   commit whose `build/`, `installer/`, `probes/`, `patches/` and
   `scripts/build.sh` match HEAD, it reuses that bundle instead, and
   `build.txt` records which of the two happened.
2. **Lab.** Copies `labs/acceptance/vmlab.wcl` into
   `.scratch/e2e/<template>/lab/`, sets the template and lab name
   (`win3d-e2e-<template>`), and runs `vmlab up`. `--host-gpu <name>`
   replaces `AMD1` with another GPU from the host config and adds
   `-<name in lower case>` to the scratch and evidence directories and the
   lab name; `--lab` names the lab outright. The lab has the
   `labs/probe/vmlab.wcl` shape for vmlab 0.25: `secure_boot = false` and
   `gpu { mode = "vulkan" host_gpu = "AMD1" }`, the AMD iGPU as the host
   config names it (`docs/host.md`). Without `host_gpu`, the lab would take
   the host's default GPU, which on helios is an RTX 5090. `vmlab up`
   replaces the templates' guest agent with the installed vmlab's. Before
   vmlab 0.31.0-alpha the lab set `agent_update = false`, because a late
   agent restart dropped the agent channel in one windows-10 run; 0.31 holds
   the channel while the agent is replaced.
   `qemu-argv-env.txt` keeps the QEMU command line and environment that vmlab
   logged, and, on vmlab 0.27, each distinct `vmlab-pci-filter:` line with
   its count. `status-v.txt` and `status-v-end.txt` keep the host GPU vmlab
   picked (`host_gpu=AMD1`) and, for a GPU with `qemu_binary`, `qemu=<path>`.
3. **Baseline.** Copies `tests/guest/`, `tests/e2e/guest/` and `out/probes/`
   into the guest and records `tests/guest/Get-GpuState.ps1`
   (`baseline-gpu-state.json`). It then creates the autologon test user with
   `tests/guest/Set-TestDesktop.ps1 -Reboot`, which first waits for
   Windows' user OOBE to finish (`test-desktop.txt`, `oobe:` line; see
   "Findings").
4. **Install.** Runs the documented command from `docs/install.md`,
   `scripts/install-into-vm.sh <lab-dir> <vm>`, unchanged (`install.log`,
   `install-journal.log`, `installed-gpu-state.json`). The script then checks
   that autologon survived the install's restarts (`autologon-*.txt`). If it
   did not, the script logs `autologon fallback fired`, saves the OOBE lines
   of `setupact.log` (`autologon-fallback.txt`) and runs
   `Set-TestDesktop.ps1 -Reboot` again.
5. **Session tests.** These run as the logged-on user in session 1, through
   `tests/guest/Run-Probe.ps1`. First the script presses Esc to close the
   Start menu, then it runs:
   - `vulkaninfo --summary` from System32 and SysWOW64, the loader's own
     tool installed by the package (`tests/e2e/guest/Invoke-Vulkaninfo.ps1`).
     LunarG's vulkaninfo 1.4.363, pinned in `tests/e2e/PINS` and run without
     its own loader, is an extra cross-check of the installed loader and ICD.
   - The D3D9, D3D11, D3D12, OpenGL and Vulkan probes (`probes/README.md`),
     x64 and x86, for 20 s each. Run-Probe samples each process's loaded
     modules (`probes/<arch>-<api>_probe.modules.txt`). While a probe runs,
     `vmlab vm screenshot` captures the console every 2 s.
     `tests/e2e/findprobe.py` looks in each capture for the probe's three
     marker squares. The capture from the middle of the run is kept as
     `console-<arch>-<api>_probe.png`, at half size.
   - `tests/guest/Get-Dxdiag.ps1 -AsUser` (`dxdiag/dxdiag.json`).
6. **Collect.** Copies the results out of the guest into
   `docs/evidence/acceptance/<template>/`, and `tests/e2e/check.py` judges
   them (`results.json`, `results.txt`). With `--apps`, the stretch
   applications run after this, so one that hangs the guest cannot cost
   the Done evidence.
7. **Teardown.** Records the `qemu.log` lines and `vmlab status -v` again,
   then runs `vmlab destroy`, unless `--keep` was given.

### How each Done item is judged

| Done item (`docs/brief.md`) | Check (`tests/e2e/check.py`) |
| --- | --- |
| 1. One script builds the whole package | `scripts/build.sh` exits 0, or the bundle it built matches HEAD's sources |
| 2. One documented install command | `scripts/install-into-vm.sh` exits 0 and the guest's `state.json` says `Installed` |
| 2. `vulkaninfo` lists the Venus device | The installed x64 `vulkaninfo --summary` names a `Virtio-GPU Venus` device that is not a CPU device |
| 2. D3D9, D3D11, D3D12 and OpenGL applications render on the GPU, with no app-local graphics DLLs, and show in a console screenshot | For each API, x64 and x86: the probe exits 0 and its readback matches; the adapter is not a software renderer and is ours (`Hardsoft VirtIO GPU 3D controller` for D3D, Zink on `Virtio-GPU Venus` for OpenGL); no module loads from the exe's directory; every graphics module comes from System32, SysWOW64, a subdirectory of either, or the driver store; the process loads our stack (`viogpu_d3d10.dll`; `d3d12.dll` with `vmlab-win3d\vkd3dcore.dll` and `libvulkan_virtio.dll`; `viogpu_wgl.dll` with `libvulkan_virtio.dll`); and at least one console capture shows the probe window's three marker squares |
| 2. `dxdiag` reports hardware acceleration | `Get-Dxdiag.ps1`'s `hardware_acceleration` is true, which requires D3D and DirectDraw `Enabled`, a full (not display-only) device, and not the Basic Render Driver. The card or driver must also be `viogpu3d` / Hardsoft |
| 3. `docs/` explains install, uninstall, the stack and pins, signing, known limits and the evidence | Not checked by the script; see "Done item 3" below |

Rows marked "extra" are reported but do not decide the result: the x86
vulkaninfo, exactly one Venus device in the x64 vulkaninfo (added for the
NVIDIA runs, so the AMD tables below predate it), the LunarG cross-check, the Vulkan probes, the device state after
install, and dxdiag's Direct3D status on its own.

## Host stack

Each run records the host in `docs/evidence/acceptance/<template>/host.txt`.
For the 0.1.1-alpha-rc3 runs (2026-10-19), which made every current table
below except windows-10 on AMD1 (rc2, vmlab 0.28.0-alpha, Resizable BAR
off):

| Item | Value |
| --- | --- |
| Host GPUs used for Venus | `AMD1`: AMD Granite Ridge iGPU, PCI `0000:72:00.0`, `/dev/dri/renderD129`, RADV. `Nvidia1`: RTX 5090, `0000:01:00.0`, `renderD128`. `Nvidia2`: RTX 5090, `0000:05:00.0`, `renderD130` |
| QEMU | AMD1: 11.1.1 (`qemu-full 11.1.1-1`) from `PATH`. Nvidia1 and Nvidia2: the host config's `qemu_binary`, `~/.local/opt/qemu-vmlab-egl`, QEMU 11.1.50 (VMLabDev/qemu `d8a6048aae11`, the commit `patches/host/PINS` pins) |
| virglrenderer | 1.3.0 (`1.3.0-2.1`) |
| Mesa / RADV | 26.2.2 (`mesa 3:26.2.2-2`, `vulkan-radeon 3:26.2.2-2`) |
| NVIDIA driver | 610.57.04 (`nvidia-utils 610.57.04-1`) |
| RTX 5090 BAR1 | 32 GiB on both cards: Resizable BAR on since 2026-10-19 (`docs/evidence/rc3/rebar/nvidia-smi-memory.txt`). Earlier NVIDIA runs had 256 MiB |
| NVIDIA render server | Nvidia1 and Nvidia2: the host config's `qemu_env` `RENDER_SERVER_EXEC_PATH`, `~/.local/opt/virglrenderer-vmlab` (virglrenderer 1.3.0 with `patches/host/virglrenderer/0001` and `0002`) |
| vmlab | 0.30.0-alpha |
| Kernel | 7.2.3-1-cachyos |

`status-v.txt` records the GPU vmlab picked: `host_gpu=AMD1`, or
`host_gpu=Nvidia1` / `Nvidia2` with
`qemu=/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64`.
`qemu-argv-env.txt` has QEMU's command line, its environment (on NVIDIA
with `RENDER_SERVER_EXEC_PATH`) and the `vmlab-pci-filter:` line. Each NVIDIA run kept only its own card (`kept
NVIDIA GeForce RTX 5090, hid 1`), and every guest listed exactly one Venus
device.

## Results

### Release candidate 0.1.1-alpha-rc3 (NVIDIA with Resizable BAR)

**Release candidate 0.1.1-alpha-rc3 passes every Done check in items 1 and
2 on both RTX 5090s, with helios's new Resizable BAR, and on AMD1: four
runs, 12 of 12 each.** BAR1 is now 32 GiB on both cards. NVIDIA's Vulkan
driver offers the device-local, host-visible type on the VRAM heap, so
`patches/mesa/0070` no longer hides it, and Venus guests on NVIDIA now use
VRAM-mapped memory. These are the first NVIDIA runs with that behaviour.
[`docs/evidence/rc3/`](evidence/rc3/README.md) has the release record and
every result below in more detail.

- `scripts/release.sh 0.1.1-alpha-rc3` built the package from a fresh clone
  of `work/rc3` at `ee9ce49`, with the shared test certificate
  (`WIN3D_SIGNING_DIR`), on the first attempt. The guest package has the
  same sources as rc2 (`0d3eb68`); only `patches/host/build-virglrenderer.sh`
  and documentation have changed since. QEMU is the same pinned commit,
  `d8a6048aae11`.
- Assets (`docs/evidence/rc3/release/SHA256SUMS`):
  `vmlab-win3d-0.1.1-alpha-rc3-windows-x64.zip`
  `e39728c8d0ee24c81c91e86033b90349b3dc995d8a0ea55d806ba389df59917c`;
  `qemu-vmlab-egl-0.1.1-alpha-rc3-x86_64-linux.tar.zst`
  `b244b68a565e038f14ab81dd0a77e3899dd4ba6bf5240e81139e00edbb0484d2`;
  `virglrenderer-vmlab-0.1.1-alpha-rc3-x86_64-linux.tar.zst`
  `3bb8a796be1837ac2c0c3e247ccd1360be2a3f8bfd295a308e6462bec53d3fd9`;
  `RELEASE-NOTES.md`
  `885ac9357546494b9c4c25a7aa43f1306b98a59cb3f37f8fc872cad42172754d`;
  `SHA256SUMS`
  `98f1bb9e1ef4b3c4e9140d8dc5c780ca0c605d4d3b273a62f622b3143ce1475b`.
  The zip holds the same `manifest.json`
  (`b36cbda671e192ad27659157d6d19707407d43cf872cc2d3a6f79d61ac770ef0`,
  commit `ee9ce49`) as the bundle every run installed (`build.txt`, mode
  `reused`, built at `ee9ce49`).
- The NVIDIA runs used helios's host config: the patched QEMU
  (`qemu=/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64`, the
  pinned commit) and the vmlab render server
  (`RENDER_SERVER_EXEC_PATH=/home/wil/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server`,
  byte-identical to the rc3 asset). Each run kept only its own card
  (`vmlab-pci-filter: … kept NVIDIA GeForce RTX 5090, hid 1`), and every
  guest listed exactly one Venus device.

| Template | Nvidia1 | Nvidia2 | AMD1 |
| --- | --- | --- | --- |
| windows-11 | **12 of 12**; every app renders | **12 of 12**; apps render, Blender's kept capture black | **12 of 12** (sanity); apps render, Blender's kept capture black |
| windows-10 | **12 of 12**; every app renders, Blender opaque (second attempt) | not run | not rerun; rc2's run stands |

- **Blender's kept capture** is black on windows-11 Nvidia2 and AMD1: the
  capture from two thirds of the way in shows its window before it has drawn.
  Rerun in the kept Nvidia2 lab, Blender showed the splash and then the full
  UI in every capture after the second, so it renders. The first launch in
  a fresh guest was most likely still starting
  ([rc3 README](evidence/rc3/README.md#blenders-kept-capture-is-black-on-windows-11-nvidia2-and-amd1)).
- **The first windows-10 Nvidia1 attempt hung in `vmlab cp`** during the
  install: 16 of 29 files copied, with no progress for 20 minutes. It was
  stopped and rerun in a fresh lab, which passed. That is a vmlab problem; the
  evidence for a vmlab report is in
  [`docs/evidence/rc3/vmlab-cp-hang/`](evidence/rc3/vmlab-cp-hang/README.md).
- **Start-up was slow on windows-11**, on AMD1 as well as NVIDIA: the four
  windows-11 runs, the compatibility sweep and a load lab booted together,
  and some first probe launches waited 8 to 53 s for Defender's cloud check
  ([known limits](known-limits.md#the-first-d3d12-launch-in-a-new-guest-waits-on-defenders-cloud-check)).
  The probes count their 20 s from the first frame, so all passed, but
  their `avg_fps` in the tables below includes the wait. Measured from the
  first frame, on windows-11 D3D9, D3D11 and Vulkan run at 60 fps on both
  cards, OpenGL at 65 to 68 and D3D12 at 46 to 56. In rc1 the windows-11
  D3D9 probe ran at 43 to 52 fps on NVIDIA. On windows-10 Nvidia1, D3D9
  now runs at 51 to 52 fps, up from 27 and 46
  ([rc3 README](evidence/rc3/README.md#e2e-runs)).

The DirectDraw fullscreen and fixed-function suites ran on Nvidia1, with
44 to 57 flips per second at 32 bpp (26 to 44 in rc1) and the known
fixed-function results. The compatibility sweep passed **12 of 12 on
Nvidia2**, up from 5 of 12
([`compat.md`](compat.md#release-candidate-011-alpha-rc3-on-nvidia2)). The
**ReBAR load check** ran three Windows VMs on Nvidia2 at once: the e2e
run, the sweep and a probe loop, then a 15-minute stage with three probe
loops. The host logged **no `can't alloc VA space` line and no Xid**.
BAR1 peaked at 764 MiB of 32 GiB, and all 116 probe runs of the final
stage passed ([rc3 README](evidence/rc3/README.md#rebar-load-check)).

The rc3 runs replaced the evidence in `windows-11-nvidia1/`,
`windows-11-nvidia2/`, `windows-10-nvidia1/` and `windows-11/` (AMD1). The
earlier runs are in git history at `ee9ce49`: rc1 on Nvidia1, the nvva
proof run on Nvidia2 and rc2 on AMD1.

### Release candidate 0.1.1-alpha-rc2 (AMD1)

**Release candidate 0.1.1-alpha-rc2 passes every Done check in items 1 and
2 on AMD1, on both templates: two runs, 12 of 12 each, and every stretch
application renders.** The NVIDIA part of rc2 ran after helios's
Resizable BAR change (`docs/plan.md`, D5), as release candidate rc3
(above). The rc3 sanity run on AMD1 replaced rc2's `windows-11/`
evidence; rc2's `windows-10/` evidence stands.

- `scripts/release.sh 0.1.1-alpha-rc2` built the package from a fresh clone
  of `main` at `0d3eb68`, with the shared test certificate
  (`WIN3D_SIGNING_DIR`). Since rc1 it adds Mesa patches 0031, 0032, 0043,
  0044, 0060, 0065 to 0067, 0070, 0075, 0080, 0085, 0086 and 0090 to 0092,
  vkd3d-proton 0006, and the render-server asset (virglrenderer 1.3.0 with
  `patches/host/virglrenderer/0001` and `0002`). QEMU is the same pinned
  commit, `d8a6048aae11`. `docs/evidence/rc2/` has the release record.
- Assets (`docs/evidence/rc2/release/SHA256SUMS`):
  `vmlab-win3d-0.1.1-alpha-rc2-windows-x64.zip`
  `16450f1959179df0957f088b4dabe845b4387e0cb17e837e2f70e80822a1bee0`;
  `qemu-vmlab-egl-0.1.1-alpha-rc2-x86_64-linux.tar.zst`
  `fc16d08cdd66fcfebd99e5c342f3a07b6fd0351c7b2e4012cb19c64460556678`;
  `virglrenderer-vmlab-0.1.1-alpha-rc2-x86_64-linux.tar.zst`
  `3355ffe48d72ad895b853fa60cd58c274d0a23fc6aefa597a16ae6e8a89145c7`;
  `RELEASE-NOTES.md`
  `0ea1aad50a2b3dc8c8d81d64e1f9a3bc3836f5ba8b753265ee5ab4182dbac6e0`.
  The zip holds the same `manifest.json`
  (`7de55b27748d335e0cd47d4b0d52c0a8faeb5a3295ac8de87495b5d0fac658f2`,
  commit `0d3eb68`) as the bundle both runs installed.
- Both runs reused that bundle (`build.txt`, mode `reused`, built at
  `0d3eb68`). They record commit `66809ce`, which adds only documentation.
- The two runs shared AMD1: they started 20 s apart, so their probes ran
  with two VMs on the iGPU. The windows-11 D3D12 and x86 D3D9 rates are
  lower than rc1's for that reason (table below); the probes still pass.

| Template | AMD1 (rc2) |
| --- | --- |
| windows-11 | **12 of 12**; every app renders |
| windows-10 | **12 of 12**; every app renders, Blender opaque |

Probe frame rates (x64, x86) and `startup_seconds`, the time from the
probe's start to its first frame (`probes/<arch>-<api>_probe.json`):

| Probe | w11 fps | w11 `startup_seconds` | w10 fps | w10 `startup_seconds` | rc1 w11 / w10 fps |
| --- | --- | --- | --- | --- | --- |
| D3D9 | 59.7, 52.2 | 0.13, 3.01 | 59.7, 59.5 | 0.12, 0.17 | 59.6, 58.4 / 59.2, 59.3 |
| D3D11 | 59.9, 59.8 | 0.10, 0.12 | 59.9, 59.8 | 0.09, 0.14 | 59.9, 59.9 / 59.8, 59.8 |
| D3D12 | 51.5, 43.3 | 3.24, 5.96 | 58.6, 59.3 | 0.45, 0.24 | 58.1, 58.2 / 57.1, 57.7 |
| OpenGL | 57.5, 63.8 | 3.67, 1.46 | 67.6, 68.0 | 0.33, 0.20 | 65.7, 65.1 / 67.0, 67.4 |
| Vulkan | 59.8, 59.7 | 0.15, 0.15 | 59.8, 59.9 | 0.13, 0.11 | 59.8, 59.7 / 59.7, 59.8 |

On windows-11 four probes took 1.5 to 6 s to their first frame, the
longest x86 D3D12. That is the first load of new unsigned images, which
Defender holds until its cloud check answers ([known
limits](known-limits.md#the-first-d3d12-launch-in-a-new-guest-waits-on-defenders-cloud-check));
[`x86d3d12.md`](x86d3d12.md) measured 2.2 to 7.2 s for x86 D3D12 in the
same step. On windows-10 every probe started in under 0.5 s.

The DirectDraw fullscreen and both D3D9 fixed-function suites ran in the
kept windows-11 lab with the results of rc1 or better, and the
compatibility sweep, in its own lab, passed 12 of 12 on AMD1, up from 10
of 12 ([`docs/evidence/rc2/`](evidence/rc2/README.md),
[`compat.md`](compat.md#release-candidate-011-alpha-rc2-on-amd1)).

### Release candidate 0.1.1-alpha-rc1

**Release candidate 0.1.1-alpha-rc1 passes every Done check in items 1 and
2, on both templates and all three host GPUs: five runs, 12 of 12 each.**
All five ran one package:

- `scripts/release.sh 0.1.1-alpha-rc1` built it from a fresh clone of commit
  `0ee5ba1` (main at `82d8e51`, plus `installer/VERSION` 0.1.1 and a
  `release.sh` version pattern that accepts `-rc1`), with the shared test
  certificate (`WIN3D_SIGNING_DIR`). Mesa patches 0001 to 0017, 0020 to
  0027, 0030, 0035 and 0040 to 0042; KMD patches 0001 and 0002; the
  installer's FeatureScore and device pin; QEMU at the pinned master-based
  commit. `docs/evidence/rc/` has the release record.
- Assets (`docs/evidence/rc/release/SHA256SUMS`):
  `vmlab-win3d-0.1.1-alpha-rc1-windows-x64.zip`
  `150406672d6e43373c643e940acf97337e058e094f45ea2c4eed2660e85205f0`;
  `qemu-vmlab-egl-0.1.1-alpha-rc1-x86_64-linux.tar.zst`
  `940ceefdd96966952526bc5c793828e4b970eb761b110d69edc984690c601be5`;
  `RELEASE-NOTES.md`
  `b27109e8098fb352bd453b2da80e832d3e4c25a21fe844960b3278c29faab67e`.
  The zip holds the same `manifest.json`
  (`b9523c1b4c1127300288530ef684434fa56d947057b780b01ac2ce2d6c097339`) as
  the bundle the runs installed.
- Every run reused that bundle (`build.txt`, mode `reused`, built at
  `0ee5ba1`). Runs that started after `0c0788c` record that commit; it
  changes only `tests/e2e/` (below).

| Template | AMD1 | Nvidia1 | Nvidia2 |
| --- | --- | --- | --- |
| windows-11 | **12 of 12**; apps render | **12 of 12**; apps render | **12 of 12**; apps render |
| windows-10 | **12 of 12**; apps render | **12 of 12**; apps render | not run |

The rc2 and rc3 runs replaced rc1's evidence. rc1's AMD1 evidence is in
git history at `0d3eb68`, and its Nvidia1 evidence at `ee9ce49`. The rc1
Nvidia2 run, with its apps, is in git history at `f1c5219`. The nvva
worker's proof run on Nvidia2 (package built at `a9579f1`, Mesa 0070 and
0086, no apps step; [`nvva.md`](nvva.md)) replaced it, and rc3 then
replaced that run.

The windows-11 runs lost their first `--apps` pass to slow downloads, and
the AMD1 and Nvidia2 runs were restarted once:

- Run in parallel, the first windows-11 runs on AMD1 and Nvidia2 wrote the
  same download file, and the LunarG runtime failed its sha256 check. They
  were stopped before the install, their labs destroyed, and both rerun
  from a fresh lab. `0c0788c` gives each run its own download file and its
  own apps staging directory.
- Helios's direct downloads from Microsoft and blender.org ran at about
  1 KB/s that night. The three windows-11 runs reached `--apps` before the
  DirectX SDK and Blender were in the cache, so their apps step was stopped
  after the Done evidence was collected, with the labs kept (`--keep`). The
  pinned files were then fetched through a proxy, from Blender mirrors for
  Blender, and checked against `tests/e2e/PINS`. After a reboot of each lab,
  `tests/e2e/apps.sh` ran against it (`apps/apps-rerun.log`). The
  windows-10 runs had the cache and ran `--apps` straight through.

<!-- e2e:results -->
### windows-11

Host GPU: AMD1, /dev/dri/renderD129 0000:72:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Granite Ridge [Radeon Graphics] (rev c1). QEMU: QEMU emulator version 11.1.1 (from PATH).

Run 2026-10-19T17:54:59+10:00 at commit `ee9ce49fefc7`, template `x86_64/windows-11@26100.1742.5`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-11/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.44 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 33.06 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.53 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.24 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 36.66 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 17.28 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 36.89 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 37.04 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.48 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.49 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-10

Host GPU: AMD1, /dev/dri/renderD129 0000:72:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Granite Ridge [Radeon Graphics] (rev c1). QEMU: QEMU emulator version 11.1.1 (from PATH).

Run 2026-10-19T13:06:39+10:00 at commit `66809ce4ec3f`, template `x86_64/windows-10@19045.2006.8`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-10/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.71 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.52 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.88 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.75 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 58.62 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 59.26 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 67.57 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)) (MESA_RADV)), 67.97 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.82 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO)), 59.89 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-10-nvidia1

Host GPU: Nvidia1, /dev/dri/renderD128 0000:01:00.0 VGA compatible controller: NVIDIA Corporation GB202 [GeForce RTX 5090] (rev a1). QEMU: /home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64, QEMU emulator version 11.1.50 (vmlab d8a6048aae11) (the Nvidia1 qemu_binary).

Run 2026-10-19T18:34:28+10:00 at commit `ee9ce49fefc7`, template `x86_64/windows-10@19045.2006.8`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-10-nvidia1/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 48.05 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 48.17 fps, readback ok, 4 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 54.36 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.69 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 48.09 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 50.28 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 64.01 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 61.23 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 55.55 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 54.23 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-11-nvidia1

Host GPU: Nvidia1, /dev/dri/renderD128 0000:01:00.0 VGA compatible controller: NVIDIA Corporation GB202 [GeForce RTX 5090] (rev a1). QEMU: /home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64, QEMU emulator version 11.1.50 (vmlab d8a6048aae11) (the Nvidia1 qemu_binary).

Run 2026-10-19T17:54:47+10:00 at commit `ee9ce49fefc7`, template `x86_64/windows-11@26100.1742.5`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-11-nvidia1/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 56.34 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 32.32 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.8 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.83 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 32.66 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 15.37 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 36.51 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 36.43 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 57.86 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 56.76 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |

### windows-11-nvidia2

Host GPU: Nvidia2, /dev/dri/renderD130 0000:05:00.0 VGA compatible controller: NVIDIA Corporation GB202 [GeForce RTX 5090] (rev a1). QEMU: /home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64, QEMU emulator version 11.1.50 (vmlab d8a6048aae11) (the Nvidia2 qemu_binary).

Run 2026-10-19T17:54:47+10:00 at commit `ee9ce49fefc7`, template `x86_64/windows-11@26100.1742.5`. **PASS: 12 of 12 Done checks pass** (run stage: ok). Evidence: `docs/evidence/acceptance/windows-11-nvidia2/`; `results.txt` is the script's own table.

| Done item | Check | Result | Evidence | Note |
| --- | --- | --- | --- | --- |
| 1 | scripts/build.sh builds the package | **pass** | `build.txt` | reused; exit 0 |
| 2 | documented install command (scripts/install-into-vm.sh) | **pass** | `install.log` | exit 0; Installed |
| 2 (extra) | display device on our driver, problem code 0 | **pass** | `installed-gpu-state.json` | Hardsoft VirtIO GPU 3D controller |
| 2 | vulkaninfo --summary (x64) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x86) lists the Venus device | **pass** | `vulkaninfo/vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | vulkaninfo --summary (x64) lists exactly one Venus device | **pass** | `vulkaninfo/vulkaninfo-x64.txt` | 1 Venus: Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x64, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x64.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 (extra) | LunarG vulkaninfo 1.4.363 (x86, pinned) lists Venus through the installed loader | **pass** | `vulkaninfo/lunarg-vulkaninfo-x86.txt` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090) |
| 2 | Direct3D 9 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d9_probe.run.json`, `probes/x64-d3d9_probe.modules.txt`, `console-x64-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 56.59 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 9 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d9_probe.run.json`, `probes/x86-d3d9_probe.modules.txt`, `console-x86-d3d9_probe.png` | Hardsoft VirtIO GPU 3D controller, 32.1 fps, readback ok, 5 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d11_probe.run.json`, `probes/x64-d3d11_probe.modules.txt`, `console-x64-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.81 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 11 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d11_probe.run.json`, `probes/x86-d3d11_probe.modules.txt`, `console-x86-d3d11_probe.png` | Hardsoft VirtIO GPU 3D controller, 57.78 fps, readback ok, 6 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-d3d12_probe.run.json`, `probes/x64-d3d12_probe.modules.txt`, `console-x64-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 31.88 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | Direct3D 12 (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-d3d12_probe.run.json`, `probes/x86-d3d12_probe.modules.txt`, `console-x86-d3d12_probe.png` | Hardsoft VirtIO GPU 3D controller, 14.57 fps, readback ok, 11 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-opengl_probe.run.json`, `probes/x64-opengl_probe.modules.txt`, `console-x64-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 45.16 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | OpenGL (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-opengl_probe.run.json`, `probes/x86-opengl_probe.modules.txt`, `console-x86-opengl_probe.png` | zink Vulkan 1.4(Virtio-GPU Venus (NVIDIA GeForce RTX 5090) (NVIDIA_PROPRIETARY)), 44.6 fps, readback ok, 10 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x64) renders on the GPU and shows on the console | **pass** | `probes/x64-vulkan_probe.run.json`, `probes/x64-vulkan_probe.modules.txt`, `console-x64-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 57.58 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 (extra) | Vulkan (x86) renders on the GPU and shows on the console | **pass** | `probes/x86-vulkan_probe.run.json`, `probes/x86-vulkan_probe.modules.txt`, `console-x86-vulkan_probe.png` | Virtio-GPU Venus (NVIDIA GeForce RTX 5090), 57.43 fps, readback ok, 7 graphics modules all in system paths, 0 app-local; console: window found, animating |
| 2 | dxdiag reports hardware acceleration on our driver | **pass** | `dxdiag/dxdiag.json` | hardware_acceleration=True, card 'Hardsoft VirtIO GPU 3D controller', D3D Enabled, DDraw Enabled, WDDM 2.0 |
| 2 (extra) | dxdiag: Direct3D acceleration on our full WDDM device | **pass** | `dxdiag/dxdiag.json` | D3D Status Enabled, Full Device (POST), WDDM 2.0 |
<!-- /e2e:results -->

## Stretch: unmodified third-party applications

`tests/e2e/run.sh --apps` (that is, `tests/e2e/apps.sh`) also runs real
applications, unmodified, from official downloads pinned by URL and sha256 in
`tests/e2e/PINS`. Each runs in the user's session through `Run-Probe.ps1`
(`-Raw`) for 30 to 90 s and is then closed. `vmlab vm screenshot` captures
the console every 4 s, and the capture from two thirds of the way through is
kept. These results do not count towards the exit status. Evidence:
`docs/evidence/acceptance/<template>/apps/`.

The prerequisites are installed system-wide, as a game installer would do
it: the DirectX End-User Runtime from the SDK's own `Redist\DXSETUP.exe
/silent` (D3DX 9/10/11 and D3DCompiler_43), and the Visual C++ 2010 SP1
runtime (`MSVCR100.dll`) that the SDK samples import (`apps/dxsetup.txt`).

The 0.1.1-alpha-rc1 runs, on five labs. Each cell names the capture in
`docs/evidence/acceptance/<run>/apps/`. The rc2 runs on AMD1 repeated the
AMD1 cells on both templates with the same result: every application
renders, ran until it was closed, and loaded no app-local graphics DLL.
The rc3 runs repeated the NVIDIA cells and windows-11 on AMD1 with
Resizable BAR on. Every application rendered and loaded no app-local graphics DLL. On
windows-11 Nvidia2 and AMD1, Blender's kept capture shows its window
before it had drawn, and a rerun in the Nvidia2 lab rendered the full UI
([rc3](evidence/rc3/README.md#blenders-kept-capture-is-black-on-windows-11-nvidia2-and-amd1)).
The captures in `windows-11/apps/` and the `*-nvidia*` directories are
now rc3's, and `windows-10/apps/` is rc2's.

| Application (pinned in `tests/e2e/PINS`) | API | windows-11: AMD1, Nvidia1, Nvidia2 | windows-10: AMD1, Nvidia1 | Modules |
| --- | --- | --- | --- | --- |
| DirectX SDK June 2010 `BasicHLSL.exe`, x64 | D3D9 | **renders** on all three: the animated, lit, textured "tiny" model with DXUT's UI; the overlay reads `HAL (hw vp): Hardsoft VirtIO GPU 3D controller` (`console-d3d9-x64-BasicHLSL.png`) | **renders** on both, same | `d3d9.dll`, `viogpu_d3d10.dll`, D3DX 9 from System32; 0 app-local |
| Same, x86 | D3D9 | **renders** on all three, same, through SysWOW64 (`console-d3d9-x86-BasicHLSL.png`) | **renders** on both | as above, SysWOW64 |
| DirectX SDK `ParallaxOcclusionMapping.exe`, x64 | D3D9 | **renders** on all three: the textured, parallax-occlusion-mapped stone disc, HAL on our device (`console-d3d9-x64-ParallaxOcclusionMapping.png`) | **renders** on both | as above |
| DirectX SDK `BasicHLSL11.exe`, x64 | D3D11 | **renders** on all three: the lit, textured "tiny" model in an 800×600 window; the overlay reads `HARDWARE: Hardsoft VirtIO GPU 3D controller` (`console-d3d11-x64-BasicHLSL11.png`) | **renders** on both | `d3d11.dll`, `dxgi.dll`, `viogpu_d3d10.dll`, D3DX 11 from System32; 0 app-local |
| DirectX SDK `SubD11.exe`, x64 | D3D11 | **renders** on all three: the tessellated character with the green wire overlay, on `HARDWARE: Hardsoft VirtIO GPU 3D controller` (`console-d3d11-x64-SubD11.png`) | **renders** on both | as above |
| Blender 4.2.3 LTS, `blender.exe --factory-startup` | OpenGL 4.3 | **renders**, opaque, on all three: the complete UI, 3D viewport, grid and gizmos (`console-opengl-x64-Blender.png`) | **renders, opaque**, on both, with the splash still in front; the wallpaper no longer shows through (`patches/mesa/0027`) | `opengl32.dll`, `viogpu_wgl.dll` with `libvulkan_virtio.dll`; app-local only Blender's own non-graphics DLLs (`python311.dll`, `python3.dll`, `blender_cpu_check.dll`, `cycles_kernel_oneapi_aot.dll`) |

On NVIDIA every application ran to its time limit. The host kernel logged
no Xid during any rc run on Nvidia1 or Nvidia2
(`docs/evidence/rc/host-kernel-nvrm.txt`). Before 0.1.1, `BasicHLSL` hung
the guest with Xid 69 on both cards ([`xid69.md`](xid69.md)), so the NVIDIA
runs had no application results.

Every application ran as the logged-on user in session 1 and was still
running when it was closed (`timed_out: true` in `<app>.run.json`). The
module lists are in `<app>.modules.txt`.

**Both D3D11 samples now render in the e2e run on both templates.** The
earlier acceptance runs (`b3e1798`, `cb4d53c`) recorded both as failures.
`docs/d3d11-apps.md` found two causes:

- SubD11 hit three UMD bugs, fixed by `patches/mesa/0015` to `0017`.
- The June 2010 DXUT places BasicHLSL11's window off the screen on Windows
  8 and later unless the SDK's `D3D11Ref.dll` is present. That is an
  application bug ([known limits](known-limits.md)). `tests/e2e/apps.sh`
  installs `D3D11Ref.dll` from the pinned SDK's developer runtime into
  System32 and SysWOW64, as the SDK installer would (`apps/dxsetup.txt`
  gives the hashes).

DXUT loads `D3D11Ref.dll` only while it enumerates devices. It is not in
either sample's module list, and both overlays name the HARDWARE device.

**Not attempted here: D3D12.** (Done later, with clang-cl and the MSVC headers:
[d3d12app.md](d3d12app.md).) The candidate was Microsoft's
DirectX-Graphics-Samples `D3D12HelloTriangle` (MIT), built from source with
the project toolchain, llvm-mingw. It does not compile unmodified with that
toolchain:

- The sample calls `ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart()`
  in the MSVC return-value form. DirectX-Headers deliberately declares the
  MinGW-ABI form, with a `RetVal` out-parameter, for non-MSVC Windows
  compilers.
- It uses `Microsoft::WRL::Wrappers::FileHandle`, which mingw's WRL does not
  provide.
- It uses MSVC's `_uuidof`.

Building it unmodified needs MSVC or clang-cl with the Windows SDK. Patching
the source would defeat the purpose, so this stretch item is skipped.

## Findings from these runs

Found by earlier runs, fixed on main, and confirmed by the final runs:

- **vulkaninfo crashed** (0xC0000005) in the package's own
  `vulkaninfo.exe`, x64 and x86, as soon as a Venus device enumerated.
  Vulkan-Tools sets up `User32Handles` only under `_MSC_VER`, so the
  llvm-mingw build dereferenced a null `user32_handles` when it created its
  surface window. LunarG's vulkaninfo worked with the same loader and ICD.
  Fixed by `patches/vulkan-tools/0001`.
- **dxdiag reported DirectDraw "Not Available"**, so `hardware_acceleration`
  was false even though D3D was `Enabled`. Fixed by
  `patches/mesa/0012`–`0014` (the D3D9 UMD's DirectDraw HAL).
- **The DirectX SDK's D3D11 (DXUT11) samples did not show.** Fixed by
  `patches/mesa/0015`–`0017` and explained by DXUT's window placement, as
  described under "Stretch" above. Both now render in the e2e run on both
  templates. The hypothesis recorded here earlier, DXGI output and mode
  enumeration, was wrong (`docs/d3d11-apps.md`).

Open:

- **Winlogon autologon values can vanish across a reboot (cause found,
  guarded).** On fresh windows-11 clones, `AutoAdminLogon` and
  `DefaultUserName` were already gone after `Set-TestDesktop.ps1 -Reboot`,
  before the install ran: in one of the four earlier windows-11 runs and in
  all three windows-11 boots of the AMD final round. vmlab traced it to
  Windows' user OOBE, which deletes those values when it exits and can still
  be running when the clone is reported ready (handoff `20261018-092229`;
  [known limits](known-limits.md#autologon-values-can-vanish-on-a-fresh-clone)).
  `Set-TestDesktop.ps1` now waits for OOBE first. In the five rc1 runs
  the wait took 0 to 25 s (`test-desktop.txt`), the values survived
  (`autologon-*.txt`), and the fallback did not fire.
- **D3D9 ran at about 30 fps** on AMD1 (27 to 34) and about 20 on NVIDIA
  in the 0.1.0 runs, against about 60 for the other APIs. In the rc1 runs
  the D3D9 probe reached 57 to 60 fps on AMD1, 51 to 52 on Nvidia1 (windows-11)
  and 40 to 42 on Nvidia2, after `patches/kmd/0002` and `patches/mesa/0026`
  ([`present.md`](present.md)). On windows-10 Nvidia1 the x64 D3D9 probe
  measured 27 fps and the x86 one 46. Below the refresh rate on NVIDIA:
  [known limits](known-limits.md).
- **Blender's window was translucent on Windows 10** in the 0.1.0 runs:
  the wallpaper showed through its viewport and panels, because Windows
  10's compositor honours the alpha Blender leaves in its framebuffer. With
  `patches/mesa/0027` (WGL presents windows opaque), the rc1 windows-10
  runs on AMD1 and Nvidia1 show Blender opaque
  (`windows-10/apps/console-opengl-x64-Blender.png`,
  `windows-10-nvidia1/apps/console-opengl-x64-Blender.png`).

Fixed after the earlier runs; the final runs exercise part of the fix:

- **`tests/guest/Run-Probe.ps1`** failed on `-Raw` with no `-ArgumentList`,
  because `New-ScheduledTaskAction` rejects an empty `-Argument`. An exit
  code of 0x80000000 or above, such as 0xC0000135 for a missing DLL, threw an
  Int32 conversion error instead of being reported. Commit `cca767b` passes
  `-Argument` only when there is one, and reinterprets the unsigned task
  result as the signed exit code (run.json also gets `exit_code_hex`). Both
  were checked without a guest, in PowerShell 7.4.2 in Docker: all guest
  scripts parse (`Test-GuestScripts.ps1`), and the conversion maps 0xC0000135
  and 0x80000000 to their signed values where the old cast throws. In the
  final runs, the guest's Windows PowerShell 5.1 ran the fixed script for
  every probe and application, with exit codes 0 and 124. A `-Raw` run with
  no arguments, or an exit code of 0x80000000 or above, did not come up.

## Other hosts

These runs used the AMD iGPU on helios. The same Done checks also pass on an
Intel Iris Xe host (`docs/zeus.md`, 12 of 12 with `patches/mesa/0021`) and
on helios's RTX 5090 (below). [Known limits](known-limits.md#host-gpu-support)
has the matrix.

### NVIDIA RTX 5090

Helios has two RTX 5090s, `Nvidia1` (`0000:01:00.0`) and `Nvidia2`
(`0000:05:00.0`). Both need the patched QEMU as their `qemu_binary`, or the
console stays black ([`nvidia.md`](nvidia.md), fault 2). The rc3 runs, with
Resizable BAR on (BAR1 32 GiB), replaced the rc1 runs below
([rc3](#release-candidate-011-alpha-rc3-nvidia-with-resizable-bar)):

```sh
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1 --lab win3d-rc3-w11-nvidia1 --apps --keep
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab win3d-rc3-w11-nvidia2 --apps --keep
tests/e2e/run.sh --template windows-10 --host-gpu Nvidia1 --lab win3d-rc3-w10-nvidia1b --apps
```

The rc1 runs, with BAR1 at 256 MiB:

```sh
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1 --lab win3d-rc-w11-nv1 --apps --keep
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab win3d-rc-w11-nv2 --apps --keep
tests/e2e/run.sh --template windows-10 --host-gpu Nvidia1 --lab win3d-rc-w10-nv1 --apps
```

They passed **12 of 12 each**, with one Venus device, the PCI filter on the
run's own card and every application rendering, with textured meshes. Their
evidence is in git history (above); the tables above are rc3's. Before 0.1.1 the NVIDIA runs differed from AMD1 in three ways, all
fixed in this package:

- The D3D9 `BasicHLSL` sample hung the guest with host Xid 69, so the
  NVIDIA runs had no application results. Fixed by `patches/mesa/0022`
  ([`xid69.md`](xid69.md)).
- After that fix the D3D9 samples' meshes rendered black. Fixed by
  `patches/mesa/0023` to `0025` ([`nvtex.md`](nvtex.md)).
- The D3D9 probe ran at about 20 fps ([`present.md`](present.md)).

The 0.1.0-alpha NVIDIA evidence (vmlab 0.27 and 0.28, package built at
`08ff884` and `99ad0c1`, the stock-QEMU Nvidia2 run with its black
console) is in git history at `82d8e51`.

## Evidence index

Every directory under `docs/evidence/`. "Historical" marks evidence from the
vmlab 0.23 era, when labs needed `qemu_args` workarounds (`docs/host.md`). It
is still valid for what it shows, but the package and the lab setup have
changed since.

| Directory | What it proves | vmlab | Package or commit | Status |
| --- | --- | --- | --- | --- |
| [`acceptance/windows-11/`](evidence/acceptance/windows-11/) | Done items 1 and 2 on a fresh `windows-11` lab on AMD1: install, vulkaninfo, the D3D9, D3D11, D3D12, OpenGL and Vulkan probes on the console, dxdiag, 12 of 12; the stretch apps in `apps/` (Blender's kept capture black, see rc3) | 0.30.0-alpha, `host_gpu = "AMD1"` | 0.1.1-alpha-rc3, the bundle `scripts/release.sh` built at `ee9ce49`; rc2's run is in git history at `ee9ce49` | Current |
| [`acceptance/windows-10/`](evidence/acceptance/windows-10/) | The same on a fresh `windows-10` lab on AMD1: 12 of 12; apps, Blender opaque | 0.28.0-alpha, `host_gpu = "AMD1"` | 0.1.1-alpha-rc2 (`0d3eb68`), run at `66809ce` | Current |
| [`acceptance/windows-11-nvidia1/`](evidence/acceptance/windows-11-nvidia1/) | The same on the first RTX 5090 with Resizable BAR, the patched QEMU as its `qemu_binary` and the vmlab render server: 12 of 12, one Venus device; every app renders, no Xid | 0.30.0-alpha, `host_gpu = "Nvidia1"` | 0.1.1-alpha-rc3 (`ee9ce49`) | Current |
| [`acceptance/windows-10-nvidia1/`](evidence/acceptance/windows-10-nvidia1/) | The same on a fresh `windows-10` lab on the first RTX 5090 with Resizable BAR: 12 of 12, one Venus device; every app renders, Blender opaque | 0.30.0-alpha, `host_gpu = "Nvidia1"` | 0.1.1-alpha-rc3 (`ee9ce49`), lab `win3d-rc3-w10-nvidia1b` | Current |
| [`acceptance/windows-11-nvidia2/`](evidence/acceptance/windows-11-nvidia2/) | The same on the second RTX 5090 with Resizable BAR: 12 of 12, one Venus device, the PCI filter on `0000:05:00.0`, with two other loaded VMs on the card (the ReBAR load check); apps render, Blender's kept capture black (see rc3) | 0.30.0-alpha, `host_gpu = "Nvidia2"` | 0.1.1-alpha-rc3 (`ee9ce49`); the nvva proof run is in git history at `ee9ce49` | Current |
| [`rc3/`](evidence/rc3/README.md) | Release candidate 0.1.1-alpha-rc3 on NVIDIA with Resizable BAR: the release record (five asset hashes, notes, `BUILDINFO`, the host assets' install check), DirectDraw fullscreen and both D3D9 fixed-function suites on Nvidia1, the ReBAR load check (three VMs on Nvidia2, BAR1 and NVRM monitor), the Blender rerun and the `vmlab cp` hang | 0.30.0-alpha | `ee9ce49` | Current |
| [`compat/rc3-nvidia2/`](evidence/compat/rc3-nvidia2/) | The compatibility sweep on Nvidia2 with the rc3 package, 12 of 12 ([`compat.md`](compat.md#release-candidate-011-alpha-rc3-on-nvidia2)) | 0.30.0-alpha, `host_gpu = "Nvidia2"` | `ee9ce49` | Current |
| [`rc2/`](evidence/rc2/README.md) | Release candidate 0.1.1-alpha-rc2 on AMD1: the release record (five asset hashes, notes, QEMU and render-server `BUILDINFO`, the host assets' install check), DirectDraw fullscreen rates and both D3D9 fixed-function suites with that package | 0.28.0-alpha | `0d3eb68` | Current |
| [`compat/rc2-amd1/`](evidence/compat/rc2-amd1/) | The compatibility sweep on AMD1 with the rc2 package ([`compat.md`](compat.md)) | 0.28.0-alpha, `host_gpu = "AMD1"` | `0d3eb68` | Current |
| [`rc/`](evidence/rc/README.md) | Release candidate 0.1.1-alpha-rc1: the release record (asset hashes, notes, QEMU `BUILDINFO`), DirectDraw fullscreen rates and the D3D9 fixed-function coverage with that package on AMD1 and Nvidia1, and the host kernel's NVRM lines during the runs | 0.28.0-alpha | `0ee5ba1` | Current |
| [`vulkaninfo/`](evidence/vulkaninfo/README.md) | The package's patched `vulkaninfo.exe` (`patches/vulkan-tools/0001`) lists Venus and exits 0, x64 and x86 | 0.24.0-alpha | `78fc688` (`work/fix-vulkaninfo`) | Current |
| [`dxdiag/`](evidence/dxdiag/) | dxdiag's DDraw and AGP status before and after `patches/mesa/0012` to `0014`, with `ddcaps` DirectDraw checks ([`dxdiag.md`](dxdiag.md)) | 0.24.0-alpha | before: `main` at `be2ef9b`; after: `deb2585` | Current |
| [`d3d11-apps/`](evidence/d3d11-apps/) | SubD11 and BasicHLSL11: the causes, `patches/mesa/0015` to `0017`, the DXUT window bug and the `D3D11Ref.dll` workaround ([`d3d11-apps.md`](d3d11-apps.md)) | 0.24.0-alpha | `5fd206d` | Current |
| [`nvidia/`](evidence/nvidia/) | The two RTX 5090 faults and their fixes, `patches/mesa/0020` and `patches/host/qemu/0001`; `05-amd-regression/` shows no AMD regression ([`nvidia.md`](nvidia.md)) | 0.24.0-alpha, isolated supervisor | `bea2a93` | Current for the diagnosis; `01-` to `03-` are diagnostic steps. The acceptance runs above replace `04-guest-fix/` as the proof that NVIDIA works |
| [`zeus/`](evidence/zeus/) | Intel Iris Xe fails on `main` and passes with `patches/mesa/0021`; the AMD regression run with 0021; the RTX 4060 failure ([`zeus.md`](zeus.md)) | 0.24.0-alpha | `main` at `db07da3`; 0021 at `1bbbfe8` | Current |
| [`package/`](evidence/package/README.md) | A fresh-clone build, then install, probes, rerun, uninstall back to the baseline and reinstall, on a fresh `windows-11` lab | 0.23, with workarounds (`lab-vmlab.wcl`) | `f7fa236` (`work/package`) | **Historical.** The bundle predates `patches/vulkan-tools/0001` and Mesa 0012 on, so the file hashes it quotes are not today's. The uninstall evidence is still the only uninstall run |
| [`kmd/`](evidence/kmd/README.md) | The KMD's test signature is accepted and the driver loads, with no Code Integrity events | 0.23 (`display = "virtio-vga-gl"`) | `e166465` | **Historical**: a kernel-only install with no UMDs, so the device showed problem code 43; the full package's device has problem code 0 (`acceptance/*/installed-gpu-state.json`). Signing is unchanged since |
| [`probes/`](evidence/probes/README.md) | The probes and guest scripts work on Microsoft's software renderers, before any driver from this repository | 0.23, no GPU | `26ca683` | **Historical** |
| [`host/`](evidence/host/) | The host-probe captures `host.md` cites: which QEMU, display and loader settings Venus needs | 0.23, with `qemu_args` variants | `d1bad91` | **Historical**: vmlab 0.24 emits these settings itself |

## Done item 3

The script does not check item 3. These are the documents that answer it:

| Topic | Document |
| --- | --- |
| Install | `docs/install.md` |
| Uninstall | `docs/uninstall.md` (the run that proves it: `docs/evidence/package/`) |
| Component stack and upstream pins | `docs/stack.md` (pin and patch tables generated from `build/*/PINS`, `patches/host/PINS` and `patches/`; `scripts/pins-table.py --check`) and `build/*/README.md` |
| Signing decision | `docs/decisions/0001-signing.md` |
| Known limits | `docs/known-limits.md`, including the host GPU matrix |
| Evidence for item 2 | This page, the evidence index above, and the investigations it links: `docs/dxdiag.md`, `docs/d3d11-apps.md`, `docs/nvidia.md`, `docs/zeus.md` |

`scripts/check-docs.py` checks that every repository path named in `docs/`
and `README.md` exists.
