# Release candidate 0.1.1-alpha-rc3 (NVIDIA with Resizable BAR)

This run repeats the NVIDIA acceptance after helios got Resizable BAR on
2026-10-19 (`docs/plan.md`, D5). BAR1 is now 32 GiB on both RTX 5090s
(`rebar/nvidia-smi-memory.txt`). NVIDIA's Vulkan driver reports the
device-local, host-visible memory type on the 31.8 GiB VRAM heap. So
`patches/mesa/0070`, which hides only a small BAR heap, no longer hides it,
and Venus guests on NVIDIA now use VRAM-mapped memory. No earlier NVIDIA run
did. All runs took place on 2026-10-19, with one package built from
`work/rc3` at `ee9ce49`. Nothing was published.

The guest package has the same sources as rc2 (`0d3eb68`). Since then only
`patches/host/build-virglrenderer.sh` (a download check) and documentation
have changed. rc2's AMD half passed ([`../rc2/README.md`](../rc2/README.md)).

## Result

| Check | Result |
| --- | --- |
| `scripts/release.sh 0.1.1-alpha-rc3` from a fresh clone | Exit 0 on the first attempt, five files (below), 20 minutes |
| `tests/e2e/run.sh --apps`, windows-11 on Nvidia1 | **12 of 12**; every application renders |
| `tests/e2e/run.sh --apps`, windows-11 on Nvidia2 | **12 of 12**; five applications render, and Blender's kept capture is black (below) |
| `tests/e2e/run.sh --apps`, windows-10 on Nvidia1 | **12 of 12**; every application renders, Blender opaque. The first attempt hung in `vmlab cp` and was rerun in a fresh lab (below) |
| `tests/e2e/run.sh --apps`, windows-11 on AMD1 (sanity) | **12 of 12**; five applications render, and Blender's kept capture is black (below) |
| Compatibility sweep on Nvidia2 | **12 of 12** pass, up from 5 of 12 with rc1 |
| DirectDraw fullscreen on Nvidia1 | Every pixel check matches. 32 bpp: 44 to 57 flips per second, up from 26 to 44 in rc1 |
| D3D9 fixed function on Nvidia1 | `d3d9_ffp` 166 of 168 hardware VP, 168 of 168 software VP; `d3d9_ffp2` 48 of 49 and 49 of 49. The same as rc1 and rc2 |
| ReBAR load check: three Windows VMs on Nvidia2 | **No** `can't alloc VA space` line and no Xid in the whole session. BAR1 peaked at 764 MiB of 32 GiB. The final 15-minute stage of three probe loops ran 116 probe runs with no failure, at 321 to 485 MiB of BAR1 |

## The package

From a fresh clone of `work/rc3` at `ee9ce49` (`.scratch/rc3-clone`, not
committed):

```sh
WIN3D_SIGNING_DIR=/home/wil/dev/vmlab-win3d/.signing scripts/release.sh 0.1.1-alpha-rc3
```

The clone's `.scratch/cache/dl/` was seeded with the KMD's three pinned
downloads (llvm-mingw and the two 26100.6584 NuGet packages) from the main
checkout's cache. `build/kmd/build.sh` checks each against its sha256.
Everything else was downloaded. The Mesa SDK packages came at 0.5 to
0.7 MB/s. The build took 20 minutes (`release/release-stages.txt`).

| File | sha256 |
| --- | --- |
| `vmlab-win3d-0.1.1-alpha-rc3-windows-x64.zip` | `e39728c8d0ee24c81c91e86033b90349b3dc995d8a0ea55d806ba389df59917c` |
| `qemu-vmlab-egl-0.1.1-alpha-rc3-x86_64-linux.tar.zst` | `b244b68a565e038f14ab81dd0a77e3899dd4ba6bf5240e81139e00edbb0484d2` |
| `virglrenderer-vmlab-0.1.1-alpha-rc3-x86_64-linux.tar.zst` | `3bb8a796be1837ac2c0c3e247ccd1360be2a3f8bfd295a308e6462bec53d3fd9` |
| `RELEASE-NOTES.md` | `885ac9357546494b9c4c25a7aa43f1306b98a59cb3f37f8fc872cad42172754d` |
| `SHA256SUMS` | `98f1bb9e1ef4b3c4e9140d8dc5c780ca0c605d4d3b273a62f622b3143ce1475b` |

`release/` keeps `SHA256SUMS`, `RELEASE-NOTES.md`, the package's own
`SHA256SUMS` (`package-SHA256SUMS`), the bundle's `manifest.json` and both
host assets' `BUILDINFO`. The zip's `vmlab-win3d-0.1.1/manifest.json` has
sha256 `b36cbda671e192ad27659157d6d19707407d43cf872cc2d3a6f79d61ac770ef0`
and names commit `ee9ce49`. All of the runs installed that bundle. The
clone's `out/package/` and `out/probes/` were copied into this worktree's
`out/`, and every e2e run reused them (`build.txt`: `reused (out/package
built at ee9ce49…)`).

**Host assets.** `release/host-assets-check.txt`: both tarballs pass
`install.sh --check` and install into a scratch prefix. The QEMU prints
`QEMU emulator version 11.1.50 (vmlab d8a6048aae11)`, which is the
`patches/host/PINS` commit. The NVIDIA runs used the binaries that helios's host
config names (`~/.local/opt/qemu-vmlab-egl`,
`~/.local/opt/virglrenderer-vmlab`), not these assets. The render server
there is byte-identical to the rc3 asset (`69d6fc6a…`). The QEMU there is
the same pinned commit, built on 2026-10-18 (its `BUILDINFO`). The binary
differs from the rc3 build (`0adf8849…` against `297e047d…`), because the
native QEMU build is not byte-reproducible.

## e2e runs

| Run | Lab | `vmlab status -v` | `qemu.log` `vmlab: env:` | `vmlab-pci-filter:` | Done | Apps |
| --- | --- | --- | --- | --- | --- | --- |
| windows-11, Nvidia1 | `win3d-rc3-w11-nvidia1` | `host_gpu=Nvidia1 qemu=/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64` | `RENDER_SERVER_EXEC_PATH=/home/wil/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server`, `VMLAB_VK_PCI_ADDRESS=0000:01:00.0` | `0000:01:00.0: kept NVIDIA GeForce RTX 5090, hid 1` | 12 of 12 | render |
| windows-11, Nvidia2 | `win3d-rc3-w11-nvidia2` | `host_gpu=Nvidia2`, the same `qemu=` | the same server, `VMLAB_VK_PCI_ADDRESS=0000:05:00.0` | `0000:05:00.0: kept NVIDIA GeForce RTX 5090, hid 1` | 12 of 12 | Blender black in the kept capture |
| windows-10, Nvidia1 | `win3d-rc3-w10-nvidia1b` | `host_gpu=Nvidia1`, the same `qemu=` | the same server, `0000:01:00.0` | `0000:01:00.0: kept NVIDIA GeForce RTX 5090, hid 1` | 12 of 12 | render, Blender opaque |
| windows-11, AMD1 | `win3d-rc3-w11-amd1` | `host_gpu=AMD1` (QEMU 11.1.1 from `PATH`) | no `RENDER_SERVER_EXEC_PATH`; `VK_LOADER_DRIVERS_SELECT=*radeon*`, `0000:72:00.0` | `0000:72:00.0: kept AMD Ryzen 9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO), hid 0` | 12 of 12 | Blender black in the kept capture |

The evidence is in `docs/evidence/acceptance/windows-11-nvidia1/`,
`windows-11-nvidia2/`, `windows-10-nvidia1/` and `windows-11/`
(`status-v.txt`, `qemu-argv-env.txt`). Each NVIDIA guest listed exactly one
Venus device. The four windows-11 runs started together at 17:54, alongside
the compatibility sweep's lab and the load lab, so six VMs booted and
installed at once. The windows-11 runs waited 287 to 307 s for Windows'
user OOBE (`test-desktop.txt`), against 0 to 25 s in rc1. The autologon
fallback did not fire.

**Start-up on windows-11.** All three windows-11 guests, AMD1 among them,
took long to show the first frame of some probes: x86 D3D9 16 to 17 s,
OpenGL 9 to 17 s, x64 D3D12 8 to 10 s and x86 D3D12 41 to 53 s
(`probes/*.json`, `startup_seconds`). windows-10 started every probe in
under 2.1 s. This is the first load of new unsigned images waiting on
Defender's cloud check
([known limits](../../known-limits.md#the-first-d3d12-launch-in-a-new-guest-waits-on-defenders-cloud-check)),
which takes longer with six fresh guests asking at once. It happens on
AMD1 as well, so the GPU is not the cause. The probes count their 20 s from
the first frame, so every probe passed. Their `avg_fps` includes the wait.
Measured from the first frame (`frames_presented` over `elapsed_seconds`
minus `startup_seconds`), the rates are:

| Probe (x64, x86) | w11 Nvidia1 | w11 Nvidia2 | w10 Nvidia1 | w11 AMD1 |
| --- | --- | --- | --- | --- |
| D3D9 | 59.9, 59.9 | 59.9, 59.8 | 52.1, 51.4 | 60.0, 60.1 |
| D3D11 | 60.0, 60.1 | 60.0, 60.0 | 56.6, 59.6 | 60.1, 60.1 |
| D3D12 | 47.5, 56.3 | 47.3, 46.2 | 52.3, 55.3 | 51.5, 52.5 |
| OpenGL | 68.0, 68.1 | 67.6, 64.7 | 67.3, 65.8 | 67.6, 67.7 |
| Vulkan | 60.3, 60.2 | 60.2, 60.3 | 58.5, 57.1 | 60.2, 60.2 |

In rc1 the D3D9 probe ran at 51 to 52 fps on Nvidia1 and 43 on Nvidia2
(windows-11), and 27 and 46 on windows-10 Nvidia1 (`avg_fps`, which then
included little start-up).

### Blender's kept capture is black on windows-11 Nvidia2 and AMD1

`apps.sh` keeps the console capture from two thirds of the way into
Blender's 90 s. In the windows-11 Nvidia2 and AMD1 runs, that capture shows
Blender's maximised window black, with only its title bar
(`apps/console-opengl-x64-Blender.png`). Blender ran until the script closed it,
and loaded the package's OpenGL stack and no app-local graphics DLL.
Nvidia1 (windows-11 and windows-10) shows the full UI. The same package
showed the full UI on AMD1 in rc2.

The Nvidia2 lab was kept, so Blender was run there again, in the same way
(`blender-rerun-nvidia2/run.sh`: Run-Probe for 90 s, Esc at the eighth
capture, a capture every 4 s, all kept). The window is black only in the
second capture, while it is created. From the third capture on, it shows
the splash and then the full UI with the default cube, until it is closed
(`blender-rerun-nvidia2/frames-every-4s.png`, `frame-016.png`). So the
second launch renders. The first launch in each fresh guest was most
likely still starting at the two-thirds point, held by the same cloud check
as the probes. The kept capture alone cannot prove that, and the AMD1 lab
was destroyed before a rerun.

### `vmlab cp` hung on the first windows-10 Nvidia1 attempt

In `win3d-rc3-w10-nvidia1`, `scripts/install-into-vm.sh` ran `vmlab cp` of
the bundle to `C:\vmlab-win3d` at 18:06:27. It stopped after 16 of 29 files
(30.5 of 102.4 MB): every top-level and `runtime\` file arrived, and nothing
under `driver\`. Twenty minutes later it had made no progress, and
`vmlab exec` still answered. The process was killed. The run went on without the
package and was stopped. The lab was destroyed, and the run was repeated in a fresh lab,
`win3d-rc3-w10-nvidia1b`, which passed. Five other VMs copied the same
bundle at the same time without trouble. The process state, the guest's
file list, `vmlab status -v` and the lab daemon log are in
[`vmlab-cp-hang/`](vmlab-cp-hang/README.md) for a vmlab report. This is a
vmlab problem, not one in this package.

## Compatibility sweep

`tests/apps/compat/run.sh --host-gpu Nvidia2 --keep` in its own lab,
`win3d-rc3-compat-nvidia2`. The evidence is in
`docs/evidence/compat/rc3-nvidia2/`; the first sweep's `nvidia2/` is kept.
**12 of 12 runs pass**, against 5 of 12 with rc1. Every
application rendered and exited cleanly on WM_CLOSE, except Crispy Doom,
which asks "press y to quit" by design. No run wrote a WER report or a host
kernel line. The table is in
[`compat.md`](../../compat.md#release-candidate-011-alpha-rc3-on-nvidia2).
The sweep shared Nvidia2 with two other VMs for its whole run (the ReBAR
load check below).

## DirectDraw fullscreen and D3D9 fixed function

`run-ddraw-ffp.sh <lab> nvidia1` (rc2's script with rc3 paths) ran in the kept
windows-11 Nvidia1 lab. During the run, the windows-10 rerun was installing
on the same card. Flips per second:

| Run | Nvidia1 rc3 | Nvidia1 rc1 |
| --- | --- | --- |
| x64, 120 frames, checked every 30: 640×480×32, 800×600×32, 1024×768×32 (`x64-rate/`) | 54.8, 57.3, 55.6 | 43.9, 42.7, 39.5 |
| x64 800×600×32, 300 frames, every frame checked (`x64-full/`) | 44.4 | 25.9 |
| x64 640×480×16 (`x64-full/`) | `DDERR_UNSUPPORTED` (`0x80004001`), as before | the same |
| x86 800×600×32 and 640×480×16, every frame checked (`x86-32and16/`) | 48.4 and 22.2 | 25.9 and 18.1 |
| x86 640×480×16 and 800×600×16, every frame checked (`x86-16bpp/`) | 26.5 and 21.5 | 18.0 and 16.9 |

Every pixel check matched (1,200 per 300-frame mode), and every run
restored the desktop mode. `x64-full` reports `fail` for the documented x64
16 bpp limit only. The host kernel logged no NVRM line during any run
(`ddraw/nvidia1/<run>/kernel-nvrm-*.txt`, empty).

| Comparison | `d3d9_ffp` (`ffp/nvidia1/`) | `d3d9_ffp2` (`ffp2/nvidia1/`) |
| --- | --- | --- |
| x64 hardware VP against the reference | 166 of 168 | 48 of 49 |
| x86 hardware VP against the reference | 166 of 168 | 48 of 49 |
| x64 software VP against the reference with software VP | 168 of 168 | 49 of 49 |
| x64 mixed device, hardware mode, against the reference | not run | 48 of 49 |
| x64 mixed device, software mode, against the reference with software VP | not run | 49 of 49 |
| x64 hardware VP against the reference with software VP | 168 of 168 | 49 of 49 |

The failures are the known ones: `lt_spot` and `lt_spot_falloff`
([`ffp.md`](../../ffp.md)), and `vb_indexed_color`, where the reference
device itself fails with `0x88760868` ([`ffp2.md`](../../ffp2.md)).

## ReBAR load check

Three Windows 11 VMs ran on Nvidia2 at once: the e2e lab, the compatibility
sweep's lab and a probe-loop lab, `win3d-rc3-load`, made with
`load.sh setup` (a copy of `docs/evidence/nvva/src/load.sh` with rc3 lab
names). Each ran the probe loop or an application for the whole session.
`load.sh monitor` sampled each card every 10 s from 17:54 to 19:47
(`rebar/monitor-nvidia1.txt`, `monitor-nvidia2.txt`). It recorded
`nvidia-smi` BAR1 Used, the kernel's cumulative count of
`can't alloc VA space` lines and of Xid lines since boot, and the dma-buf
MiB each QEMU had mapped.

| Phase on Nvidia2 | What ran | BAR1 Used | `can't alloc VA space` | Xid | Probe runs |
| --- | --- | --- | --- | --- | --- |
| 17:55 to 19:09 | e2e windows-11 (install, probes, apps, then a probe loop from 18:36), compatibility sweep (install, 12 applications), probe loop in `load` from 18:11 | peak **764 MiB** (18:37) | 0 | 0 | `load`: 128 of 129 pass; `w11-nvidia2`: 113 of 113 to 19:21 |
| 19:03 to 19:23 | probe loops in the e2e lab and the sweep's lab; `load` until 19:09 | | 0 | 0 | `compat-nvidia2`: 42 of 42 |
| 19:24:54 to 19:40:12, controlled stage | 15-minute probe loops in all three labs at once (`rebar/stage-3vm-probe-loops/`) | 321 to 485 MiB, mean 383 | 0 | 0 | **116 of 116** pass |

Nvidia1 carried the host desktop and two VMs (windows-11, then the
windows-10 rerun). Its BAR1 peaked at 242 MiB, also with no line. Across
1,071 samples on both cards, the count stayed at 0 `can't alloc VA space`
lines and 0 Xid lines since the boot at 17:28. Each QEMU on Nvidia2 had 1.2
to 1.7 GiB of dma-buf mapped, more than the old 256 MiB aperture could
hold for one VM. The unpatched three-VM stage in
[`nvva.md`](../../nvva.md) logged 62 lines at 213 MiB, and the patched
one stood at 79 MiB of 256.

No lab's QEMU log has a render-server error: no `failed to look up object`,
CS error or failed `vkAllocateMemory`, the signs of the old BAR1 failure
([`nvva.md`](../../nvva.md)). The only lines are `vkr: destroying context …
with a valid instance` as programs exit, and two `ctrl 0x102, error 0x1203`
(`rebar/render-server-errors.txt`).

Two failures in the load lab are not BAR1 problems:

- Its first x86 D3D12 probe (18:18) timed out after 120 s without a first
  frame (`load-during-runs/fail-181959-x86-d3d12.txt`). That was the first
  32-bit D3D12 launch in that guest, which waits on Defender (above). The
  next twelve x86 D3D12 runs in that lab passed in 23 to 25 s.
- At 19:09:33 the guest shut itself down: `wlms.exe`, "The license period
  for this installation of Windows has expired", 65 minutes after boot
  (`load-lab-shutdown.txt`, read after restarting the lab). The guest was
  not activated. `load.sh setup` adds a `nic { nat = true }` to
  `labs/probe/vmlab.wcl`, which now has one, so the guest had two NICs. The
  loop then logged one immediate failure per probe until it was stopped;
  `runs.txt` ends with a note instead of those lines. The controlled stage
  ran in the restarted lab.

## Reproduce

```sh
scripts/release.sh 0.1.1-alpha-rc3        # in a fresh clone, WIN3D_SIGNING_DIR set
# copy the clone's out/package and out/probes into this checkout's out/
docs/evidence/ddraw/src/build.sh && docs/evidence/ffp/src/build.sh && docs/evidence/ffp2/src/build.sh
LABS=win3d-rc3 docs/evidence/rc3/load.sh monitor 0000:05:00.0 21600 docs/evidence/rc3/rebar/monitor-nvidia2.txt &
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1 --lab win3d-rc3-w11-nvidia1 --apps --keep
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab win3d-rc3-w11-nvidia2 --apps --keep
tests/e2e/run.sh --template windows-10 --host-gpu Nvidia1 --lab win3d-rc3-w10-nvidia1b --apps
tests/e2e/run.sh --template windows-11 --host-gpu AMD1 --lab win3d-rc3-w11-amd1 --apps
COMPAT_LAB=win3d-rc3-compat-nvidia2 COMPAT_EV=$PWD/docs/evidence/compat/rc3-nvidia2 \
  tests/apps/compat/run.sh --host-gpu Nvidia2 --keep
docs/evidence/rc3/load.sh setup load Nvidia2 && docs/evidence/rc3/load.sh load load 150 docs/evidence/rc3/rebar/load-during-runs
docs/evidence/rc3/run-ddraw-ffp.sh .scratch/e2e/windows-11-nvidia1/lab nvidia1
# the controlled stage: link each kept lab directory to .scratch/labs/<name>, then, at once:
docs/evidence/rc3/load.sh load <name> 15 docs/evidence/rc3/rebar/stage-3vm-probe-loops/<name>
```

All rc3 labs are destroyed and their GPU slots released.
