# Release candidate 0.1.1-alpha-rc1

This run checks every fix since `v0.1.0-alpha` together, in one package,
on helios, 2026-10-18 and 19. Since 0.1.0 the changes are Mesa patches 0022
to 0027, 0030, 0035 and 0040 to 0042, KMD patch 0002, the installer's
FeatureScore and device pin, and the QEMU master build in the host config.
Nothing was published.

## Result

| Check | Result |
| --- | --- |
| `scripts/release.sh 0.1.1-alpha-rc1` from a fresh clone | Exit 0, four files (below) |
| `tests/e2e/run.sh --apps`, windows-11 on AMD1, Nvidia1, Nvidia2 | **12 of 12** each; every application renders |
| `tests/e2e/run.sh --apps`, windows-10 on AMD1, Nvidia1 | **12 of 12** each; every application renders, Blender opaque |
| DirectDraw fullscreen, x64 32 bpp and x86 16 bpp | Every pixel check matches. 32 bpp: 58 to 60 flips per second on AMD1 and 40 to 44 on Nvidia1, up from 9 to 29 and 2 to 8 |
| D3D9 fixed function (`d3d9_ffp`), AMD1 and Nvidia1 | 166 of 168 with hardware vertex processing (x64 and x86), 168 of 168 with software vertex processing, as expected |
| Host Xid during the runs | None (`host-kernel-nvrm.txt`; the one line there is from another session's lab, below) |

The tables per run, and the stretch applications, are in
[`docs/acceptance.md`](../../acceptance.md).

## The package

From a fresh clone of `work/rc` at `0ee5ba1`
(`.scratch/rc-clone`, not committed):

```sh
WIN3D_SIGNING_DIR=/home/wil/dev/vmlab-win3d/.signing scripts/release.sh 0.1.1-alpha-rc1
```

`0ee5ba1` is main at `82d8e51` plus one commit. `release.sh` could not
take this version on main: `installer/VERSION` was `0.1.0`, and the
version pattern rejected a `-` inside the pre-release part. That commit
sets `installer/VERSION` to `0.1.1` and lets the pattern accept
`0.1.1-alpha-rc1`.

The build took 25 minutes from empty caches (`release/release-stages.txt`).

| File | sha256 |
| --- | --- |
| `vmlab-win3d-0.1.1-alpha-rc1-windows-x64.zip` | `150406672d6e43373c643e940acf97337e058e094f45ea2c4eed2660e85205f0` |
| `qemu-vmlab-egl-0.1.1-alpha-rc1-x86_64-linux.tar.zst` | `940ceefdd96966952526bc5c793828e4b970eb761b110d69edc984690c601be5` |
| `RELEASE-NOTES.md` | `b27109e8098fb352bd453b2da80e832d3e4c25a21fe844960b3278c29faab67e` |

`release/` keeps `SHA256SUMS`, `RELEASE-NOTES.md`, the package's own
`SHA256SUMS` (`package-SHA256SUMS`, 28 files) and the QEMU `BUILDINFO`. The
zip's `vmlab-win3d-0.1.1/manifest.json` has sha256 `b9523c1b…c097339`. That is
the same as the bundle every e2e run installed. The clone's
`out/package/` and `out/probes/` were copied into this worktree's `out/`,
and `tests/e2e/run.sh` reused them (`build.txt`: `reused (out/package
built at 0ee5ba1…)`).

**QEMU.** The tarball's QEMU is VMLabDev/qemu `d8a6048aae11`, the
`patches/host/PINS` commit, on upstream `a19f59a72d`. The NVIDIA runs used
the host config's `qemu_binary`, `~/.local/opt/qemu-vmlab-egl`, which is
an earlier build of the same commit (its `BUILDINFO` names `d8a6048aae11`).
The two binaries differ in sha256 (`0a29d781…` in the tarball, `0adf8849…`
installed), because they were built at different times and from different
source paths. The tarball was extracted, `install.sh` put it in a scratch
prefix, and `--version` printed `QEMU emulator version 11.1.50 (vmlab
d8a6048aae11)`. No guest was booted on the tarball's binary itself.

## e2e runs

| Run | Lab | `vmlab status -v` | Done | Apps |
| --- | --- | --- | --- | --- |
| windows-11, AMD1 | `win3d-rc-w11-amd1` | `host_gpu=AMD1` | 12 of 12 | render (rerun, below) |
| windows-11, Nvidia1 | `win3d-rc-w11-nv1` | `host_gpu=Nvidia1 qemu=/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64` | 12 of 12 | render (rerun) |
| windows-11, Nvidia2 | `win3d-rc-w11-nv2` | `host_gpu=Nvidia2 qemu=/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64` | 12 of 12 | render (rerun) |
| windows-10, AMD1 | `win3d-rc-w10-amd1` | `host_gpu=AMD1` | 12 of 12 | render |
| windows-10, Nvidia1 | `win3d-rc-w10-nv1` | `host_gpu=Nvidia1 qemu=/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64` | 12 of 12 | render |

The evidence is in `docs/evidence/acceptance/<run>/`, where `<run>` is
`windows-11`, `windows-11-nvidia1`, `windows-11-nvidia2`, `windows-10` or
`windows-10-nvidia1`. The probe rates:

| Probe (x64, x86) | w11 AMD1 | w11 Nvidia1 | w11 Nvidia2 | w10 AMD1 | w10 Nvidia1 |
| --- | --- | --- | --- | --- | --- |
| D3D9 | 59.6, 58.4 | 52.3, 50.8 | 40.1, 41.7 | 59.2, 59.3 | 26.9, 45.7 |
| D3D11 | 59.9, 59.9 | 58.6, 58.4 | 57.5, 57.9 | 59.8, 59.8 | 57.4, 57.8 |
| D3D12 | 58.1, 58.2 | 53.6, 53.5 | 50.5, 49.4 | 57.1, 57.7 | 51.3, 54.6 |
| OpenGL | 65.7, 65.1 | 64.5, 62.4 | 45.9, 48.2 | 67.0, 67.4 | 56.6, 65.5 |
| Vulkan | 59.8, 59.7 | 57.7, 57.7 | 56.5, 55.9 | 59.7, 59.8 | 57.6, 57.1 |

In the 0.1.0 runs the D3D9 probe ran at 27 to 34 fps on AMD1 and about 20
on NVIDIA.

### What went wrong, and how it was handled

- **Parallel runs shared one download.** The first windows-11 runs on AMD1
  and Nvidia2 started 20 s apart. Both downloaded the LunarG runtime into
  the same `.part` file in `.scratch/e2e/cache/`, and both failed its
  sha256 check before the install. They were stopped, their labs
  destroyed, and both rerun from fresh labs. Commit `0c0788c` gives each
  download its own temporary file (`tests/e2e/run.sh`, `tests/e2e/apps.sh`),
  and gives each run its own apps staging directory. Before that, parallel
  runs also shared one screenshot directory, which each run empties when
  it finishes.
- **Throttled downloads.** That night helios's direct downloads from
  `download.microsoft.com` and `download.blender.org` ran at about 1 KB/s.
  All three windows-11 runs reached `--apps` before the DirectX SDK and
  Blender were in the cache. With the Done evidence already collected,
  their apps step was stopped, and the labs were kept. The SDK was
  resumed through the local VPN proxy. Blender was fetched in ranges from
  Blender mirrors through the proxy, after a first attempt wrote mirrors'
  error pages into the ranges and failed the sha256. All four files match
  `tests/e2e/PINS`. Each lab was rebooted and `tests/e2e/apps.sh <lab>
  win <evidence>` run against it (`apps/apps-rerun.log`). The windows-10
  runs found the cache full and ran `--apps` straight through.
- **apps.sh with a relative evidence path.** The reruns passed the
  evidence directory as a relative path. `apps.sh` copies `<app>.run.json`
  from inside the lab directory, so those files landed under the lab's
  scratch directory. They were moved into `apps/`, and `apps.json` was
  rebuilt with `apps.sh`'s own summary code.
- **The expired evaluation licence did not bite.** The templates' licence
  has expired, and `wlms.exe` shuts a VM down about an hour after it
  boots. Each e2e run takes 20 to 30 minutes, the last reboot coming
  during the install. The kept windows-11 labs were rebooted before the
  DirectDraw and fixed-function runs (00:30) and before the application
  reruns (01:12), and no VM shut down during any step.
- **Other sessions shared the host.** Other sessions' builds and labs ran
  at the same time, including a lab on Nvidia2 after this run had
  destroyed its own. The host logged one Xid 69 on `0000:05:00` at
  01:50:44 (`host-kernel-nvrm.txt`). By then the last rc lab on an NVIDIA
  card, windows-10 on Nvidia1, had finished at 01:37, and the rc's Nvidia2
  lab had been destroyed at about 01:24. No NVRM line appears from 23:30
  to 01:48.

## DirectDraw fullscreen

`run-ddraw-ffp.sh <lab> <gpu>` ran `docs/evidence/ddraw/run-guest.sh` four
times in the kept windows-11 lab, with `ddfull` from
`docs/evidence/ddraw/src/build.sh`. These are the same runs as the ddraw
worker's ([`ddraw.md`](../../ddraw.md#the-repros)). Flips per second:

| Run | Nvidia1 | AMD1 |
| --- | --- | --- |
| x64, 120 frames, checked every 30: 640×480×32, 800×600×32, 1024×768×32 (`x64-rate/`) | 43.9, 42.7, 39.5 | 57.5, 60.3, 59.5 |
| x64 800×600×32, 300 frames, every frame checked (`x64-full/`) | 25.9 | 59.0 |
| x64 640×480×16 (`x64-full/`) | `DDERR_UNSUPPORTED`, as before | the same |
| x86 800×600×32 and 640×480×16, every frame checked (`x86-32and16/`) | 25.9 and 18.1 | 58.7 and 43.3 |
| x86 640×480×16 and 800×600×16, every frame checked (`x86-16bpp/`) | 18.0 and 16.9 | 42.0 and 33.6 |
| Before the present fixes, 32 bpp ([`ddraw.md`](../../ddraw.md#frame-rate)) | 2 to 8 | 9 to 29 |

Every pixel check matched (1,200 per 300-frame mode), and each run
restored the desktop mode. `ddraw/<gpu>/<run>/` has `ddfull-*.json`, the
run summary, one console capture from the middle of the run
(`console-mid.png`) and the host's NVRM lines (empty). `ddraw/<gpu>/status-v.txt`
records the lab.

## D3D9 fixed function

`docs/evidence/ffp/src/run-repro.sh` ran in the same labs, with
`d3d9_ffp.exe` from `docs/evidence/ffp/src/build.sh` and the pinned SDK's
`d3dref9.dll` (`ffp/<gpu>/d3dref9-sha256.txt`):

| Comparison | AMD1 | Nvidia1 |
| --- | --- | --- |
| x64 hardware VP against the reference (`compare-x64.txt`) | 166 of 168 | 166 of 168 |
| x86 hardware VP against the reference (`compare-x86.txt`) | 166 of 168 | 166 of 168 |
| x64 software VP against the reference with software VP (`compare-x64-swvp.txt`) | 168 of 168 | 168 of 168 |
| x64 hardware VP against the reference with software VP (`compare-x64-hw-vs-refswvp.txt`) | 168 of 168 | 168 of 168 |

The two failures are `lt_spot` and `lt_spot_falloff` on both GPUs. They are
the reference's own two vertex pipelines disagreeing on spotlight falloff
([`ffp.md`](../../ffp.md)). This is the ffp worker's result on Nvidia2 and
AMD1, now repeated on Nvidia1.

## Reproduce

```sh
scripts/release.sh 0.1.1-alpha-rc1        # in a fresh clone, WIN3D_SIGNING_DIR set
# copy the clone's out/package and out/probes into this checkout's out/
docs/evidence/ffp/src/build.sh && docs/evidence/ddraw/src/build.sh
tests/e2e/run.sh --template windows-11 --host-gpu AMD1 --lab win3d-rc-w11-amd1 --apps --keep
docs/evidence/rc/run-ddraw-ffp.sh .scratch/e2e/windows-11/lab amd1
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1 --lab win3d-rc-w11-nv1 --apps --keep
docs/evidence/rc/run-ddraw-ffp.sh .scratch/e2e/windows-11-nvidia1/lab nvidia1
tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab win3d-rc-w11-nv2 --apps
tests/e2e/run.sh --template windows-10 --host-gpu AMD1 --lab win3d-rc-w10-amd1 --apps
tests/e2e/run.sh --template windows-10 --host-gpu Nvidia1 --lab win3d-rc-w10-nv1 --apps
```

Destroy the kept labs afterwards (`vmlab destroy` in each lab directory).
All five rc labs are destroyed.
