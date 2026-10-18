#!/usr/bin/env bash
# Build the vmlab QEMU (x86_64 system emulator only) for the host, for lab
# use only.
#
#   patches/host/build-qemu.sh
#
# Builds the commit that patches/host/PINS pins ("qemu" line) on the
# VMLabDev/qemu fork's branch vmlab/egl-headless-external-dmabuf: upstream
# QEMU master plus the egl-headless fix for NVIDIA's external-only dma-bufs
# (docs/nvidia.md). Master already has the shutdown fix, baca25172d "ui/egl:
# fix render node cleanup order". It fetches exactly that commit, never the
# branch tip; patches/host/bump-qemu.sh moves the pin.
# Writes out/host/qemu/bin/qemu-system-x86_64.
#
# To test a commit before it is pushed, set QEMU_URL (any git URL or local
# clone) and QEMU_COMMIT; BUILDINFO then names that source instead of PINS.
#
# This build runs natively, not in Docker: QEMU links glib, pixman, epoxy,
# gbm and virglrenderer, and a current Arch container is already newer than
# the frozen host (glib 2.90 against the host's 2.88), so its binary would not
# load here. The build uses the host's compiler and libraries read-only, uses
# QEMU's vendored Meson, writes only under .scratch/ and out/, and installs
# nothing. It needs git, gcc, make, ninja, pkg-config and the headers of the
# libraries above, all of which the host's packages already ship. configure
# downloads the Meson subprojects (keycodemapdb, berkeley-softfloat-3, ...)
# at the revisions the commit's subprojects/*.wrap files pin.
#
# The binary is configured with --prefix=/usr, so it uses the host's
# /usr/share/qemu ROMs (through the out/host/qemu/share/qemu symlink). A lab
# uses this build when the host config names it as its GPU's qemu_binary
# (vmlab >= 0.26; docs/nvidia.md, "Host setup").
#
# For packaging (scripts/release.sh) it also writes out/host/qemu/BUILDINFO,
# the source commit, the build host and the versions of the host libraries
# the binary was built against, and copies QEMU's licence files to
# out/host/qemu/licenses/.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/out/host/qemu
WORK=$REPO/.scratch/build-qemu
read -r URL BRANCH COMMIT < <(awk '$1 == "qemu" && $3 == "branch" && $5 == "commit" { print $2, $4, $6 }' "$HERE/PINS")
URL=${QEMU_URL:-$URL}; COMMIT=${QEMU_COMMIT:-$COMMIT}
[[ ${COMMIT:-} =~ ^[0-9a-f]{40}$ ]] || { echo "no qemu branch/commit pin in $HERE/PINS" >&2; exit 1; }

[ ! -e "$WORK" ] || find "$WORK" -delete
[ ! -e "$OUT" ] || find "$OUT" -delete
mkdir -p "$WORK/qemu" "$OUT/bin"
cd "$WORK/qemu"
echo "fetching $URL $COMMIT (branch $BRANCH)"
git init -q
git fetch -q --depth 32 "$URL" "$COMMIT"
git -c advice.detachedHead=false checkout -q FETCH_HEAD
[ "$(git rev-parse HEAD)" = "$COMMIT" ] || { echo "fetched $(git rev-parse HEAD), wanted $COMMIT" >&2; exit 1; }
# The series is the run of non-merge commits on top of the upstream merge
# the branch is rebased on; the fetch depth above covers it.
# (awk reads to the end, so that git log gets no SIGPIPE under pipefail.)
SERIES=$(git log --first-parent --abbrev=10 --format='%h %p%x09%s' HEAD | awk -F'\t' '!done { if (split($1, f, " ") > 2) done = 1; else print f[1], $2 }')
BASE=$(git log --first-parent --abbrev=10 --format='%h %p' HEAD | awk '!done && NF > 2 { print $1; done = 1 }')
# The shallow clone has no tags for git describe, so name the commit instead.
./configure --prefix=/usr --target-list=x86_64-softmmu --enable-kvm \
    --with-pkgversion="vmlab ${COMMIT:0:12}" \
    --enable-opengl --enable-virglrenderer --enable-vnc --enable-tpm \
    --enable-slirp --disable-docs --disable-gtk --disable-sdl --disable-spice \
    --disable-werror >"$WORK/configure.log"
grep -E "GBM|VirGL support|OpenGL support" "$WORK/configure.log"
make -j"$(nproc)" qemu-system-x86_64
cp build/qemu-system-x86_64 "$OUT/bin/"
# QEMU looks for its ROMs in ../share/qemu next to the binary: use the host's.
mkdir -p "$OUT/share"; ln -sfn /usr/share/qemu "$OUT/share/qemu"
"$OUT/bin/qemu-system-x86_64" --version | head -1

mkdir -p "$OUT/licenses"; cp COPYING COPYING.LIB LICENSE "$OUT/licenses/"
(
    . /etc/os-release
    echo "qemu:           $("$OUT/bin/qemu-system-x86_64" --version | head -1)"
    echo "source:         $URL branch $BRANCH"
    echo "commit:         $COMMIT"
    echo "base:           ${BASE:-unknown} (upstream)"
    printf '%s\n' "$SERIES" | sed 's/^/series:         /'
    echo "build host:     ${PRETTY_NAME:-unknown}, $(uname -m), built $(date -u +%Y-%m-%d)"
    echo "compiler:       $(cc --version | head -1)"
    echo "glibc:          $(ldd --version | head -1 | awk '{print $NF}')"
    echo "needs GLIBC:    $(objdump -T "$OUT/bin/qemu-system-x86_64" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
    for m in glib-2.0 pixman-1 epoxy gbm virglrenderer libdrm slirp zlib libzstd libpng libjpeg gnutls libudev; do
        v=$(pkg-config --modversion "$m" 2>/dev/null) && printf 'lib %-11s %s\n' "$m:" "$v"
    done
    echo "NEEDED:         $(objdump -p "$OUT/bin/qemu-system-x86_64" | awk '$1 == "NEEDED" { print $2 }' | tr '\n' ' ')"
) >"$OUT/BUILDINFO"
cat "$OUT/BUILDINFO"
