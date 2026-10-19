# Release candidate 0.1.1-alpha-rc2 (AMD1)

This run checks every fix merged since 0.1.1-alpha-rc1 together, in one
package built from `main`, on helios's AMD iGPU (`AMD1`), 2026-10-19. The
NVIDIA part of rc2 runs separately, after the host's Resizable BAR change
(`docs/plan.md`, D5). Nothing was published.

Since rc1 the package adds Mesa patches 0031, 0032, 0043, 0044, 0060, 0065
to 0067, 0070, 0075, 0080, 0085, 0086 and 0090 to 0092, and vkd3d-proton
0006. The release adds a third host asset, the vmlab render server.

## Result

| Check | Result |
| --- | --- |
| `scripts/release.sh 0.1.1-alpha-rc2` from a fresh clone | Exit 0, five files (below), on the second attempt |
| `tests/e2e/run.sh --apps`, windows-11 on AMD1 | **12 of 12**; every application renders |
| `tests/e2e/run.sh --apps`, windows-10 on AMD1 | **12 of 12**; every application renders, Blender opaque |
| DirectDraw fullscreen, x64 32 bpp and x86 16 bpp | Every pixel check matches. 32 bpp: 59 to 61 flips per second. x86 16 bpp: 40 to 52, up from 34 to 43 in rc1 |
| D3D9 fixed function (`d3d9_ffp`) | 166 of 168 with hardware vertex processing (x64 and x86), 168 of 168 with software vertex processing: the same as rc1 |
| D3D9 fixed function, second suite (`d3d9_ffp2`) | 48 of 49 with hardware vertex processing (x64, x86, mixed), 49 of 49 with software; the same as the ffp2 worker's AMD1 result |
| Compatibility sweep, AMD1 | See [`compat.md`](../../compat.md) and "Compatibility sweep" below |

The tables per run, and the stretch applications, are in
[`docs/acceptance.md`](../../acceptance.md).

## The package

From a fresh clone of `main` at `0d3eb68` (`.scratch/rc2-clone`, not
committed):

```sh
WIN3D_SIGNING_DIR=/home/wil/dev/vmlab-win3d/.signing scripts/release.sh 0.1.1-alpha-rc2
```

`main` needed no change for this version: `installer/VERSION` is `0.1.1`
and the version pattern accepts `-rc2`.

The first attempt built the package and QEMU (23 minutes from empty
caches), then failed in the render-server stage. The download of the pinned
`virglrenderer-1.3.0.tar.gz` from `gitlab.freedesktop.org` returned HTTP
504, and `build-virglrenderer.sh` went on to `sha256sum` and `tar` on the
missing file. The archive was fetched through the local VPN proxy, its
sha256 matched `patches/host/PINS`
(`065bc56e…5395e76f3`), and it was put in the clone's download cache.
`release.sh` was then run again in the same clone, still clean, and
finished in three minutes from the build caches
(`release/release-stages.txt`).

| File | sha256 |
| --- | --- |
| `vmlab-win3d-0.1.1-alpha-rc2-windows-x64.zip` | `16450f1959179df0957f088b4dabe845b4387e0cb17e837e2f70e80822a1bee0` |
| `qemu-vmlab-egl-0.1.1-alpha-rc2-x86_64-linux.tar.zst` | `fc16d08cdd66fcfebd99e5c342f3a07b6fd0351c7b2e4012cb19c64460556678` |
| `virglrenderer-vmlab-0.1.1-alpha-rc2-x86_64-linux.tar.zst` | `3355ffe48d72ad895b853fa60cd58c274d0a23fc6aefa597a16ae6e8a89145c7` |
| `RELEASE-NOTES.md` | `0ea1aad50a2b3dc8c8d81d64e1f9a3bc3836f5ba8b753265ee5ab4182dbac6e0` |

`release/` keeps `SHA256SUMS`, `RELEASE-NOTES.md`, the package's own
`SHA256SUMS` (`package-SHA256SUMS`), the bundle's `manifest.json` (every
file's sha256), and both host assets' `BUILDINFO`. The zip's
`vmlab-win3d-0.1.1/manifest.json` has sha256
`7de55b27748d335e0cd47d4b0d52c0a8faeb5a3295ac8de87495b5d0fac658f2` and
names commit `0d3eb68`. That is the bundle both e2e runs and the sweep
installed: the clone's `out/package/` and `out/probes/` were copied into
this worktree's `out/`, and `tests/e2e/run.sh` reused them (`build.txt`:
`reused (out/package built at 0d3eb68…)`).

The release notes were generated from `main` at `0d3eb68`, so their
known-limits summary predates this branch's known-limits pass.

**Host assets.** `release/host-assets-check.txt`: both tarballs were
extracted, `install.sh --check` passed (libraries, ISA level x86-64-v2,
the binary runs), and each installed into a scratch prefix. The QEMU
prints `QEMU emulator version 11.1.50 (vmlab d8a6048aae11)`, the
`patches/host/PINS` commit, as in rc1. The render server starts against
the host's virglrenderer 1.3.0. No guest was booted on either asset: the
AMD1 runs use the host's own QEMU 11.1.1 and render server.

## e2e runs

| Run | Lab | `vmlab status -v` | Done | Apps |
| --- | --- | --- | --- | --- |
| windows-11, AMD1 | `win3d-rc2-w11-amd1` | `host_gpu=AMD1` | 12 of 12 | render |
| windows-10, AMD1 | `win3d-rc2-w10-amd1` | `host_gpu=AMD1` | 12 of 12 | render |

The evidence is in `docs/evidence/acceptance/windows-11/` and
`docs/evidence/acceptance/windows-10/`. The two runs started 20 s apart
and shared the iGPU; the probe rates and `startup_seconds` are in
[`acceptance.md`](../../acceptance.md#release-candidate-011-alpha-rc2-amd1).
OOBE was done before the autologon setup in both (`test-desktop.txt`,
`oobe: done after 0 s`), and the autologon fallback did not fire.

## DirectDraw fullscreen

`run-ddraw-ffp.sh <lab> amd1` ran `docs/evidence/ddraw/run-guest.sh` four
times in the kept windows-11 lab, with `ddfull` from
`docs/evidence/ddraw/src/build.sh`, then both fixed-function suites. The
compatibility sweep's lab was running on AMD1 at the same time. Flips per
second:

| Run | AMD1 rc2 | AMD1 rc1 |
| --- | --- | --- |
| x64, 120 frames, checked every 30: 640×480×32, 800×600×32, 1024×768×32 (`x64-rate/`) | 59.0, 60.0, 61.0 | 57.5, 60.3, 59.5 |
| x64 800×600×32, 300 frames, every frame checked (`x64-full/`) | 59.2 | 59.0 |
| x64 640×480×16 (`x64-full/`) | `DDERR_UNSUPPORTED` (`0x80004001`), as before | the same |
| x86 800×600×32 and 640×480×16, every frame checked (`x86-32and16/`) | 59.2 and 51.7 | 58.7 and 43.3 |
| x86 640×480×16 and 800×600×16, every frame checked (`x86-16bpp/`) | 51.1 and 40.1 | 42.0 and 33.6 |

Every pixel check matched (1,200 per 300-frame mode), and each run
restored the desktop mode. x64 16 bpp is the documented limit
([known limits](../../known-limits.md)), so `x64-full` reports `fail` for
that mode only. `ddraw/amd1/<run>/` has `ddfull-*.json`, the run summary,
one console capture from the middle of the run (`console-mid.png`) and the
host kernel's NVRM lines (empty). `ddraw/amd1/status-v.txt` and
`status-v-end.txt` record the lab.

## D3D9 fixed function

`docs/evidence/ffp/src/run-repro.sh` and
`docs/evidence/ffp2/src/run-repro.sh` ran in the same lab, with the probes
from each suite's `build.sh` and the pinned SDK's `d3dref9.dll`
(`ffp*/amd1/d3dref9-sha256.txt`).

| Comparison | `d3d9_ffp` (`ffp/amd1/`) | `d3d9_ffp2` (`ffp2/amd1/`) |
| --- | --- | --- |
| x64 hardware VP against the reference | 166 of 168 | 48 of 49 |
| x86 hardware VP against the reference | 166 of 168 | 48 of 49 |
| x64 software VP against the reference with software VP | 168 of 168 | 49 of 49 |
| x64 mixed device, hardware mode, against the reference | not run | 48 of 49 |
| x64 mixed device, software mode, against the reference with software VP | not run | 49 of 49 |
| x64 hardware VP against the reference with software VP | 168 of 168 | 49 of 49 |

The `d3d9_ffp` failures are `lt_spot` and `lt_spot_falloff`: the
reference's own two vertex pipelines disagree on spotlight falloff
([`ffp.md`](../../ffp.md)). The `d3d9_ffp2` failure is `vb_indexed_color`,
where the reference device itself fails with `0x88760868`
([`ffp2.md`](../../ffp2.md)). Both match the earlier results on AMD1
(`docs/evidence/rc/ffp/amd1/`, `docs/evidence/ffp2/after/w11-amd1/`). Each
script exits 1 because of these known comparisons.

## Compatibility sweep

COMPAT-PLACEHOLDER

## Reproduce

```sh
scripts/release.sh 0.1.1-alpha-rc2        # in a fresh clone, WIN3D_SIGNING_DIR set
# copy the clone's out/package and out/probes into this checkout's out/
docs/evidence/ddraw/src/build.sh && docs/evidence/ffp/src/build.sh && docs/evidence/ffp2/src/build.sh
tests/e2e/run.sh --template windows-11 --host-gpu AMD1 --lab win3d-rc2-w11-amd1 --apps --keep
tests/e2e/run.sh --template windows-10 --host-gpu AMD1 --lab win3d-rc2-w10-amd1 --apps
docs/evidence/rc2/run-ddraw-ffp.sh .scratch/e2e/windows-11/lab amd1
COMPAT_LAB=win3d-rc2-compat-amd1 COMPAT_EV=$PWD/docs/evidence/compat/rc2-amd1 \
  tests/apps/compat/run.sh --host-gpu AMD1
```

Destroy the kept lab afterwards (`vmlab destroy` in its lab directory).
All rc2 labs are destroyed.
