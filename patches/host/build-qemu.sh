#!/usr/bin/env bash
# Build a patched QEMU 11.1.1 (x86_64 system emulator only) for the host, for
# lab use only.
#
#   patches/host/build-qemu.sh
#
# Applies patches/host/qemu/*.patch in order to the 11.1.1 release (the host's
# package version, decision D3) and writes out/host/qemu/bin/qemu-system-x86_64.
#
# This build runs natively, not in Docker: QEMU links glib, pixman, epoxy,
# gbm and virglrenderer, and a current Arch container is already newer than
# the frozen host (glib 2.90 against the host's 2.88), so its binary would not
# load here. The build uses the host's compiler and libraries read-only, uses
# QEMU's vendored Meson, writes only under .scratch/ and out/, and installs
# nothing. It needs gcc, make, ninja, pkg-config and the headers of the libraries
# above, all of which the host's packages already ship.
#
# The binary is configured with --prefix=/usr, so it uses the host's
# /usr/share/qemu ROMs (through the out/host/qemu/share/qemu symlink). A lab
# uses this build when the host config names it as its GPU's qemu_binary
# (vmlab >= 0.26; docs/nvidia.md, "Host setup"). Pins are in patches/host/PINS.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/out/host/qemu
DL=$REPO/.scratch/downloads
WORK=$REPO/.scratch/build-qemu
URL=https://download.qemu.org/qemu-11.1.1.tar.xz
SHA=079ffbff8a7111bbc89022107cbabf3bbfd614d5fc9d7cc675991196aca12482

mkdir -p "$DL" "$OUT/bin"
tarball=$DL/qemu-11.1.1.tar.xz
[ -f "$tarball" ] || curl -fsSL -o "$tarball" "$URL"
echo "$SHA  $tarball" | sha256sum -c -

[ ! -e "$WORK" ] || find "$WORK" -delete
mkdir -p "$WORK"
tar -C "$WORK" -xf "$tarball"
cd "$WORK/qemu-11.1.1"
for p in "$HERE"/qemu/*.patch; do echo "applying $(basename "$p")"; patch -p1 <"$p"; done
./configure --prefix=/usr --target-list=x86_64-softmmu --enable-kvm \
    --enable-opengl --enable-virglrenderer --enable-vnc --enable-tpm \
    --enable-slirp --disable-docs --disable-gtk --disable-sdl --disable-spice \
    --disable-werror >"$WORK/configure.log"
grep -E "GBM|VirGL support|OpenGL support" "$WORK/configure.log"
make -j"$(nproc)" qemu-system-x86_64
cp build/qemu-system-x86_64 "$OUT/bin/"
# QEMU looks for its ROMs in ../share/qemu next to the binary: use the host's.
mkdir -p "$OUT/share"; ln -sfn /usr/share/qemu "$OUT/share/qemu"
"$OUT/bin/qemu-system-x86_64" --version | head -1
