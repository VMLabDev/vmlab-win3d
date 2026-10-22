# Distribution matrix without `-L`: the firmware through `<prefix>/share/qemu`

Run on helios on 2026-10-22 by `docs/evidence/portable/matrix.sh` (as changed
on `work/romlink`: no `-L`, the host's `/usr/share/qemu` mounted read-only at
`/usr/share/qemu`, a package-index query per distribution, and the
`+noroms` row) against the published 0.1.2-alpha tarballs
(`gh release download v0.1.2-alpha -R VMLabDev/vmlab-win3d`), with the QEMU
tarball repacked in this worktree's `.scratch/` so that its `install.sh` is
`scripts/release/qemu-install.sh` from this branch. Nothing else in it
changed; the render-server tarball is the published one.

| Tarball | sha256 |
| --- | --- |
| `qemu-vmlab-egl-0.1.2-alpha-x86_64-linux.tar.zst` (repacked) | `6187f69861e51ff37dda4ddb48a9675c04720b0465ba90139078a47561789a68` |
| `virglrenderer-vmlab-0.1.2-alpha-x86_64-linux.tar.zst` (published) | `03b1051cbfdbde343369dc2f598f0d81f22ab9051b5c7333a532a89741a6dfe00` |

**Why.** QEMU looks for `bios-256k.bin`, the option ROMs and the keymaps in
the `-L` directories, then in `<bindir>/../share/qemu`, never in
`/usr/share/qemu` by itself. The tarball ships none of them, and the old
`install.sh` created no `share/qemu`, so an installed binary run without
`-L` stopped with `qemu: could not load PC BIOS 'bios-256k.bin'` (seen on
Zeus with the 0.1.2-alpha tarball). vmlab always passes `-L`, so labs were
unaffected. The new `install.sh` checks for `/usr/share/qemu/bios-256k.bin`
in `--check` and in the install, names the package to install if it is
missing, links `<prefix>/share/qemu` to `/usr/share/qemu`, and boots the
installed binary once without `-L`.

Each row is one throwaway container, pulled by tag and recorded by digest,
with only the distribution's runtime packages for the binaries' `NEEDED`
list (plus `zstd`, and `gawk` on openSUSE). The containers have no QEMU
package, so the host's `/usr/share/qemu` is mounted read-only at
`/usr/share/qemu` and stands in for it. In each: the distribution's QEMU
package looked up in its index; both tarballs' `install.sh --check`;
`install.sh /tmp/prefix/...` for both; `ls -ld /tmp/prefix/qemu/share/qemu`;
`--version`; `qemu-system-x86_64 -machine q35 -accel tcg -display none
-nodefaults -S -qmp stdio` **with no `-L`**, answering `qmp_capabilities`
and `quit`; and the render server started with no socket. The per-row log
is `matrix/<image>.log`; `matrix/summary.txt` is the one-line summary.

| Image | Digest | glibc | QEMU package in the index | Result |
| --- | --- | --- | --- | --- |
| `debian:bookworm` | `debian@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61` | 2.36 | `qemu-system-x86` 1:7.2+dfsg-7+deb12u18+b3, depends on `qemu-system-data`, `seabios`, `ipxe-qemu` | **PASS**: both checks, both installs, `share/qemu -> /usr/share/qemu`, `--version`, QMP under TCG without `-L`, render server starts |
| `debian:trixie` | `debian@sha256:4c25275446ca371766b8b093c696782eabce06263fccacb8d5d813b3dc44b308` | 2.41 | `qemu-system-x86` 1:10.0.13+ds-0+deb13u1 | **PASS** |
| `ubuntu:24.04` | `ubuntu@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac` | 2.39 | `qemu-system-x86` 1:8.2.2+ds-0ubuntu1.18 | **PASS** |
| `fedora:latest` (Fedora 44) | `fedora@sha256:43b29f65a41eb9c35e1cd5323e3bdf3b655c2357a9f4f1ff2f9c2798e5045d80` | 2.43 | `qemu-system-x86-core` 10.2.2-1.fc44, requires `seabios-bin` | **PASS** |
| `archlinux:latest` | `archlinux@sha256:52dfbd5afb23a220d5ee92dafcb4c788d96c026ed04179890c23adef4c897fa0` | 2.44 | `qemu-base` 11.1.2-1 (depends on `qemu-common`, `qemu-system-x86`) and `qemu-full` 11.1.2-1 | **PASS** |
| `opensuse/tumbleweed` | `opensuse/tumbleweed@sha256:cbae3d25ca359a916e3dff8fd8913fe8943002baeb6f94b4ca4305e201edc43a` | 2.44 | `qemu-x86` 11.1.2-1.1, requires `qemu-seabios`, `qemu-vgabios` | **PASS** |
| `ubuntu:22.04` | `ubuntu@sha256:0a9887931b0e38af2e3487ed39e29436b0535c4f3f52e57745baaf79d1898956` | 2.35 | `qemu-system-x86` 1:6.2+dfsg-2ubuntu6.31 | **REFUSED, as required**: both `install.sh --check` exit 1 with `error: this host's glibc is 2.35; this QEMU needs glibc 2.36 or newer` |
| `debian:bookworm+noroms` (nothing mounted at `/usr/share/qemu`) | `debian@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61` | 2.36 | as above | **REFUSED, as required**: the QEMU `install.sh --check` passes glibc, ldd, the bundled library and the ISA level, then exits 1 with `error: /usr/share/qemu/bios-256k.bin is missing ...` and the four package lines (`apt install qemu-system-x86`, `dnf install qemu-system-x86-core`, `pacman -S qemu-base (or qemu-full)`, `zypper install qemu-x86`). `install.sh /tmp/prefix/qemu` refuses the same way, exit 1, and installs nothing. The render server's check passes (exit 0): it needs no firmware |

The package names `install.sh` prints are confirmed by the index queries
in the logs (the "QEMU package in the index" column): each exists in its
distribution's index, and its dependencies show where the firmware comes
from. On Debian and Ubuntu `qemu-system-x86` depends on `qemu-system-data`,
`seabios` and `ipxe-qemu`; on Fedora `qemu-system-x86-core` requires
`seabios-bin`; on openSUSE `qemu-x86` requires `qemu-seabios` and
`qemu-vgabios`; on Arch `qemu-base` depends on `qemu-common` and
`qemu-system-x86`, and `qemu-full` on the full set. On helios (CachyOS,
`qemu-full`), `pacman -Qo` says `/usr/share/qemu/bios-256k.bin` is owned by
`seabios 1.17.0-2` and `/usr/share/qemu/efi-virtio.rom` by
`qemu-common 11.1.2-1`, both in `qemu-base`'s dependency closure. RHEL and
CachyOS were not run as rows; Fedora 44 and Arch stand in for them.

The Arch and Tumbleweed images moved since the portable run of 2026-10-21
(`docs/evidence/portable/matrix.md`), so their digests differ; the other
five digests are the same.

**Zeus is out of scope here.** The user will check it later with the next
release's tarball (`docs/evidence/portable/zeus.txt` has the commands).
