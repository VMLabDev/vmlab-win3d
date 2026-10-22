# Evidence: the QEMU tarball's firmware link, and render-node numbering

Branch `work/romlink`, 2026-10-22, helios. Two slices, neither needing a
VM or a GPU slot.

## Part 1: `install.sh` provides the host's firmware

| Item | File | Result |
| --- | --- | --- |
| Distribution matrix without `-L` | [`matrix.md`](matrix.md), [`matrix/`](matrix/) | Six target rows **PASS** with the QMP boot taking its firmware from `<prefix>/share/qemu -> /usr/share/qemu`; `ubuntu:22.04` refused by the glibc floor; the new `debian:bookworm+noroms` row refused with the package message, and its install refused too. The package names are confirmed against each distribution's index |
| Helios, repacked tarball | [`helios/check.txt`](helios/check.txt), [`helios/install.txt`](helios/install.txt) | `install.sh --check` and `install.sh <prefix>` into `.scratch/romlink/prefix`: the link is made, a stale symlink is replaced, a real directory is left with a warning and the boot then fails as designed, the QMP boot without `-L` passes, and the same boot fails once the link is removed. `~/.local/opt/` untouched |
| Zeus | — | **Out of scope.** The user will check it later |

The repacked tarball is the published `qemu-vmlab-egl-0.1.2-alpha` with
`scripts/release/qemu-install.sh` from this branch as its `install.sh`
(sha256 in `matrix.md`); it lives in the worktree's `.scratch/` and is not
published. The next release build picks the script up through
`scripts/release.sh`.

What changed: `scripts/release/qemu-install.sh` (firmware check in
`--check` and install; `<prefix>/share/qemu` link; QMP boot without `-L`
after `--version`), `scripts/release/qemu-README.txt`,
`docs/evidence/portable/matrix.sh` (no `-L`; the host's `/usr/share/qemu`
mounted in place; package-index query; `+noroms` row), `docs/release.md`,
`docs/install.md`.

## Part 2: render-node numbers are per boot

Helios rebooted on 2026-10-21 and the kernel numbered the render nodes in
another order. `/sys/class/drm/renderD*/device` on 2026-10-22:

| Node | PCI | Was (before the reboot) |
| --- | --- | --- |
| `renderD128` | `0000:05:00.0` (Nvidia2) | `0000:01:00.0` (Nvidia1) |
| `renderD129` | `0000:01:00.0` (Nvidia1) | `0000:72:00.0` (AMD1) |
| `renderD130` | `0000:72:00.0` (AMD1) | `0000:05:00.0` (Nvidia2) |

Rule applied: guidance and comments name a GPU by host-config name and PCI
address only; where a render node matters, say it is resolved from the
address at run time and point at `/sys/class/drm/renderD*/device`. Records
of past runs stay as they are.

Changed: `labs/probe/vmlab.wcl`, `labs/acceptance/vmlab.wcl`,
`labs/zeus/amd/vmlab.wcl`, `labs/nvidia/vmlab.wcl` (comments);
`labs/zeus/README.md` and `docs/zeus.md` lines 31, 46–47, 112–113 (Zeus's
host-config example and GPU table); `docs/install.md` lines 21 and 39,
`docs/nvidia.md` lines 75 and 314–315, `docs/host.md` lines 72–74 (host
config examples and the reproduction step); `docs/stack.md` lines 46 and
211; `docs/plan.md` line 25; `docs/acceptance.md` line 103 (the host-stack
row); a dated note in `docs/host.md` next to its render-node table;
`build/signing/load-test.sh`, which now resolves the render node of
`KMD_LOAD_PCI_ADDRESS` (default `0000:72:00.0`) from `/sys/class/drm`,
keeps `KMD_LOAD_RENDERNODE` as the override, and has `--print-rendernode`
for a dry print ([`load-test-rendernode.txt`](load-test-rendernode.txt):
`0000:72:00.0 -> /dev/dri/renderD130` today; `bash -n` clean; no lab was
run).

Kept unchanged, from `grep -rn 'renderD12[0-9]'` outside `docs/evidence/`
and `.scratch/`, and why:

| Site | Why kept |
| --- | --- |
| `docs/host.md` render-node table (lines 159–161) | What the host-probe run of 2026-10-17 observed; the new dated note beside it says the numbers are per boot |
| `docs/host.md` lines 98, 127, 268, 287, 299, 321, 328–329, 335, 340, 393 | The 0.24 migration table (line 98 already says vmlab resolves the node through sysfs) and the host-probe run records: variant tables, the QEMU command line as run, which process held which node |
| `docs/host.md` line 165 | The new note itself, which gives today's mapping as a dated example |
| `docs/acceptance.md` lines 329, 358, 387, 416 | Per-run "Host GPU:" lines, kept by the brief |
| `docs/zeus.md` line 112 | Now states the node is resolved at run time and keeps `renderD129` only as what `intel/*/qemu-argv-env.txt` recorded |
| `docs/nvidia.md` line 14 | The "Result" paragraph records the 12-of-12 run on the first RTX 5090 at the time; the address identifies the card, and the node is what that run had |
| `docs/prior-art.md` lines 87, 139, 523–524, 537 | Prior art is a review record (`COMMON.md`: evidence, not instructions); lines 523–524 say "currently" as of that review's date. `docs/host.md` carries the per-boot note |
