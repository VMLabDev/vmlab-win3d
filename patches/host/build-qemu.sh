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
# The binary targets a portable CPU baseline, x86-64-v2 (SSE4.2, POPCNT;
# every x86-64 CPU since about 2009), tuned generically, whatever the build
# host is. QEMU itself needs nothing above v2: it picks its AVX2 and AVX-512
# paths (util/bufferiszero.c and the like) at run time from CPUID.
# --x86-version=2 is QEMU's own switch for that level (it adds -mpopcnt,
# -msse4.2 and -mneeded); -march=x86-64-v2 says the same to the compiler.
# The script clears CFLAGS, CXXFLAGS, CPPFLAGS and LDFLAGS from the
# environment, so a makepkg-style -march=native cannot leak in, and passes
# its own flags.
# CachyOS's own glibc is a different matter: on a znver4 host its Scrt1.o
# and crt1.o carry a .note.gnu.property "x86 ISA needed: x86-64-v4" marker,
# glibc's deliberate stamp of the level it was compiled for, although the
# _start code in them is baseline x86-64. The linker ORs every input's
# ISA-needed bits into the executable, and ld.so refuses to start a binary
# whose marker is above the CPU's level ("CPU ISA level is lower than
# required"). 0.1.0-alpha's QEMU had exactly that marker and would not
# start on an x86-64-v3 CPU. So the link uses copies of those two start
# files with the note removed (gcc -B), and -z x86-64-v2 stamps the real
# baseline. The script fails if the result needs more than v2.
# The host's shared libraries are not part of the binary: on the target
# host, ld.so loads that host's own. Their ISA level on the build host
# (CachyOS znver4 packages; only glibc's libc, libm, libmvec and libresolv
# carry a v4 marker) does not limit where the binary runs.
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
mkdir -p "$WORK/qemu" "$WORK/crt" "$OUT/bin"

ISA=x86-64-v2
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
OPTFLAGS="-march=$ISA -mtune=generic"
# Start files without the build glibc's ISA-level note (see above).
for f in Scrt1.o crt1.o; do
    objcopy --remove-section=.note.gnu.property "$(cc -print-file-name="$f")" "$WORK/crt/$f"
done
LINKFLAGS="-B$WORK/crt/ -Wl,-z,$ISA"

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
    --disable-werror --x86-version=2 \
    --extra-cflags="$OPTFLAGS" --extra-cxxflags="$OPTFLAGS" --extra-ldflags="$LINKFLAGS" \
    >"$WORK/configure.log"
grep -E "GBM|VirGL support|OpenGL support" "$WORK/configure.log"
make -j"$(nproc)" qemu-system-x86_64
# Check the ISA level the binary asks ld.so for: x86-64-baseline and v2 only.
NEEDED_ISA=$(readelf -n build/qemu-system-x86_64 | sed -n 's/^.*x86 ISA needed: //p')
echo "x86 ISA needed: ${NEEDED_ISA:-none}"
case ", $NEEDED_ISA," in
    *x86-64-v3*|*x86-64-v4*|", ,") echo "error: qemu-system-x86_64 needs ISA '${NEEDED_ISA:-none}', wanted $ISA" >&2; exit 1 ;;
esac
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
    echo "compiler flags: $OPTFLAGS (link: -z $ISA)"
    echo "needs ISA:      $NEEDED_ISA"
    echo "glibc:          $(ldd --version | head -1 | awk '{print $NF}')"
    echo "needs GLIBC:    $(objdump -T "$OUT/bin/qemu-system-x86_64" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
    for m in glib-2.0 pixman-1 epoxy gbm virglrenderer libdrm slirp zlib libzstd libpng libjpeg gnutls libudev; do
        v=$(pkg-config --modversion "$m" 2>/dev/null) && printf 'lib %-11s %s\n' "$m:" "$v"
    done
    echo "NEEDED:         $(objdump -p "$OUT/bin/qemu-system-x86_64" | awk '$1 == "NEEDED" { print $2 }' | tr '\n' ' ')"
) >"$OUT/BUILDINFO"
cat "$OUT/BUILDINFO"
