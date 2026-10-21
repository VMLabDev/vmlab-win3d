# Evidence: portable host assets

The two host release assets, the vmlab QEMU (`qemu-vmlab-egl`) and the
render server (`virglrenderer-vmlab`), built in a Debian 12 container with a
minimal QEMU feature set so that they run on any x86-64 distribution with
glibc 2.36 or newer ([`docs/release.md`](../../release.md#supported-hosts-of-the-host-assets)).
Branch `work/portable`, 2026-10-21, helios. Before this, both assets linked
helios's own libraries (QEMU needed `GLIBC_2.43` and 55 shared libraries)
and an Arch libnfs update broke the first 0.1.1-alpha QEMU.

| Item | File | Result |
| --- | --- | --- |
| 1. ABI | [`abi.txt`](abi.txt) | QEMU, the bundled `libvirglrenderer.so.1` and the render server all need at most `GLIBC_2.34`; `x86 ISA needed: x86-64-baseline, x86-64-v2`; QEMU's `NEEDED` is ten libraries (`libvirglrenderer.so.1` bundled, `RUNPATH=$ORIGIN/../lib`), the server's two (`libgbm.so.1`, `libc.so.6`) |
| Feature set | [`features.md`](features.md), [`config-host.txt`](config-host.txt) | Every QEMU option, device, netdev, chardev, display, blockdev driver and QMP command vmlab emits, mapped to a configure switch or to no library |
| 2. Distribution matrix | [`matrix.md`](matrix.md), [`matrix/`](matrix/), [`matrix.sh`](matrix.sh) | PASS on `debian:bookworm`, `debian:trixie`, `ubuntu:24.04`, `fedora:latest` (44), `archlinux:latest`, `opensuse/tumbleweed` with only the runtime packages; `ubuntu:22.04` refused with the glibc message. Images pinned by digest |
| 3. Helios, real GPU | [`e2e-nvidia1/`](e2e-nvidia1/) | `tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1` through an isolated supervisor with both release tarballs installed into a scratch prefix: **12 of 12**; `status-v.txt` shows `qemu=<prefix>/qemu-vmlab-egl/bin/qemu-system-x86_64` and `env=RENDER_SERVER_EXEC_PATH=<prefix>/virglrenderer-vmlab/libexec/virgl_render_server`; `kernel-log.txt`: no Xid, no NVRM line |
| 4. Zeus | [`zeus.txt`](zeus.txt) | **Unmet.** Zeus is online but its only SSH is Tailscale SSH in check mode, which asks for a browser login that a non-interactive session cannot give; its LAN sshd refuses helios's key. The file has the outputs and the commands to run once a session is possible |
| 5. Release path | [`release.txt`](release.txt), [`RELEASE-NOTES-0.1.1-alpha-portable1.md`](RELEASE-NOTES-0.1.1-alpha-portable1.md) | `scripts/release.sh 0.1.1-alpha-portable1` in a fresh clone of the branch at `53b8044`, exit 0 in 285 s (cached downloads and images); `SHA256SUMS` and stage timings; its tarballs are the ones the matrix and the e2e run used. `out/release/` left in the clone, unpublished |
| 6. Docs | `docs/release.md`, `docs/install.md`, the two tarball `README.txt` | State the floor, the package names, how to check a host (`install.sh --check`) and how to build outside the floor; `scripts/check-docs.py` reports 0 missing |

What changed in the repository: `patches/host/Dockerfile`,
`patches/host/container.sh` (new), `patches/host/build-qemu.sh`,
`patches/host/build-virglrenderer.sh`, `patches/host/PINS` (image digest,
Python 3.12 pin), `scripts/release.sh` (render server before QEMU; `lib/`
in the QEMU tarball), `scripts/release/qemu-install.sh` and
`virgl-install.sh` (glibc floor, bundled-library check, `lib/` install),
the two `README.txt` templates, and the docs above.

Decisions recorded on the way:

- The ISA-note handling for CachyOS start files was dead in the container
  (Debian's `Scrt1.o`/`crt1.o` carry only `x86-64-baseline`) and was
  dropped; `-z x86-64-v2` and the post-build check stay.
- QEMU master's configure needs Python 3.12; bookworm has 3.11, so the image
  adds a python-build-standalone 3.12 pinned by URL and sha256 as a build
  tool. Nothing built links it.
- The glibc floor is enforced by `install.sh` as the build glibc (2.36),
  although the binaries' symbols stop at `GLIBC_2.34`: the supported set is
  what was built against and tested, and `WIN3D_ALLOW_OLDER_GLIBC=1` exists
  for an unsupported try (Ubuntu 22.04 ran `--version` that way).
- The host's virglrenderer version check in `virgl-install.sh` became
  informational: the pair is the QEMU asset's bundled library and this
  server, both from one build. The library's compiled-in fallback server
  path is `/usr/libexec/virgl_render_server`, so a GPU block with the
  `qemu_binary` must also carry the `qemu_env`.
- `libslirp` is no longer linked: vmlab never emits `-netdev user` (its own
  userspace switch is a `-netdev stream` peer).
