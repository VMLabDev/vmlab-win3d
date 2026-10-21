# Distribution matrix of the two host assets

Run on helios on 2026-10-21 by `matrix.sh` against the two tarballs of the
release `0.1.1-alpha-portable1` built in a fresh clone (`release.txt`;
`qemu-vmlab-egl-0.1.1-alpha-portable1-x86_64-linux.tar.zst` sha256
`cbb7808f…0f920`, `virglrenderer-vmlab-0.1.1-alpha-portable1-x86_64-linux.tar.zst`
sha256 `03e8523e…44eaa`). The same script on the pre-release staging of
the same `out/host/` build gave the same seven results (`.scratch` run,
not kept).

Each row is one throwaway Docker container, pulled by tag and recorded by
digest, with **only the distribution's runtime packages for the binaries'
`NEEDED` list installed** (plus `zstd` to unpack the tarballs and, on
openSUSE, `gawk`, which `install.sh` needs and the image lacks). In each:
both tarballs' `install.sh --check`; `install.sh /tmp/prefix/...` for both;
`qemu-system-x86_64 --version`; `qemu-system-x86_64 -machine q35 -accel tcg
-display none -nodefaults -S -qmp stdio` answering `qmp_capabilities` and
`quit` (with `-L /roms`, the host's `/usr/share/qemu` mounted read-only,
since the containers have no QEMU package and vmlab passes the host QEMU's
data directory the same way); and the render server started with no
socket, which is what its `install.sh --check` runs. The per-row log is
`matrix/<image>.log`; `matrix/summary.txt` is the one-line summary.

| Image | Digest | glibc | Runtime packages installed | Result |
| --- | --- | --- | --- | --- |
| `debian:bookworm` | `debian@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61` | 2.36 | `libglib2.0-0` 2.74.6, `libpixman-1-0` 0.42.2, `libepoxy0` 1.5.10, `libgbm1` 22.3.6, `zlib1g` 1.2.13, `libzstd1` 1.5.4, `libusb-1.0-0` 1.0.26 | **PASS**: both checks, both installs, `--version`, QMP under TCG, render server starts |
| `debian:trixie` | `debian@sha256:4c25275446ca371766b8b093c696782eabce06263fccacb8d5d813b3dc44b308` | 2.41 | `libglib2.0-0t64` 2.84.4, `libpixman-1-0` 0.44.0, `libepoxy0`, `libgbm1`, `zlib1g` 1.3.1, `libzstd1` 1.5.7, `libusb-1.0-0` 1.0.28 | **PASS** |
| `ubuntu:24.04` | `ubuntu@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac` | 2.39 | `libglib2.0-0t64` 2.80.0, `libpixman-1-0` 0.42.2, `libepoxy0`, `libgbm1`, `zlib1g` 1.3, `libzstd1` 1.5.5, `libusb-1.0-0` 1.0.27 | **PASS** |
| `fedora:latest` (Fedora 44) | `fedora@sha256:43b29f65a41eb9c35e1cd5323e3bdf3b655c2357a9f4f1ff2f9c2798e5045d80` | 2.43 | `glib2` 2.88.3, `pixman` 0.46.2, `libepoxy`, `mesa-libgbm`, `zlib-ng-compat` 1.3.1, `libzstd` 1.5.7, `libusb1` 1.0.29 | **PASS** |
| `archlinux:latest` | `archlinux@sha256:e43f1fd80e073992c5f173efbbbf65144170bf60aeb7369940087d63eacea8f4` | 2.44 | `glib2` 2.90.1, `pixman` 0.46.4, `libepoxy`, `mesa`, `zlib` (zlib-ng 1.3.2), `zstd` 1.5.7, `libusb` 1.0.29 | **PASS** |
| `opensuse/tumbleweed` | `opensuse/tumbleweed@sha256:049953b95bb89ebe61239f1313b59d73c59a0373a6113de138835aa6b0b67851` | 2.44 | `libglib-2_0-0` 2.88.4, `libpixman-1-0` 0.46.4, `libepoxy0`, `libgbm1`, `libz1` 1.3.1, `libzstd1` 1.5.7, `libusb-1_0-0` 1.0.29 | **PASS** (first attempt failed only because the script named the pixman package `libpixman-1-0-0` and the image has no `awk`; fixed in `matrix.sh`) |
| `ubuntu:22.04` | `ubuntu@sha256:0a9887931b0e38af2e3487ed39e29436b0535c4f3f52e57745baaf79d1898956` | 2.35 | `libglib2.0-0` 2.72.4, `libpixman-1-0` 0.40.0, `libepoxy0`, `libgbm1`, `zlib1g` 1.2.11, `libzstd1` 1.4.8, `libusb-1.0-0` 1.0.25 | **REFUSED, as required**: both `install.sh --check` exit 1 with `error: this host's glibc is 2.35; this QEMU needs glibc 2.36 or newer` (the render server's says the same) and name the supported distributions and the two ways out. Informational: with `WIN3D_ALLOW_OLDER_GLIBC=1` the check continues and `--version` runs, since the binary's own symbols need only `GLIBC_2.34` |

The six supported rows prove the floor from both sides: Debian 12 is the
exact build glibc and the oldest libraries (glib 2.74, mesa 22.3, zstd
1.5.4), and Arch and Tumbleweed are the newest (glib 2.90, glibc 2.44).
The library sonames are the same on all seven images, so the only
distribution-specific knowledge the tarballs need is the package names,
which `README.txt` and `docs/release.md` list.

RHEL 9 and 10 were not run (no free container image with the GPU stack to
hand); Fedora 44 stands in for RHEL 10's family. RHEL 9's glibc 2.34 is
below the floor the same way Ubuntu 22.04's is.
