#!/usr/bin/env bash
# Build the vmlab QEMU (x86_64 system emulator only) for the host release
# asset, for lab use only.
#
#   patches/host/build-qemu.sh
#
# Builds the commit that patches/host/PINS pins ("qemu" line) on the
# VMLabDev/qemu fork's branch vmlab/egl-headless-external-dmabuf: upstream
# QEMU master plus the egl-headless fix for NVIDIA's external-only dma-bufs
# (docs/nvidia.md). Master already has the shutdown fix, baca25172d "ui/egl:
# fix render node cleanup order". It fetches exactly that commit, never the
# branch tip; patches/host/bump-qemu.sh moves the pin.
# Writes out/host/qemu/bin/qemu-system-x86_64 and out/host/qemu/lib/.
#
# To test a commit before it is pushed, set QEMU_URL (any git URL or local
# clone) and QEMU_COMMIT; BUILDINFO then names that source instead of PINS.
#
# Portable build. The fetch runs on the host; configure and make run in the
# Debian 12 container of patches/host/Dockerfile (patches/host/container.sh;
# glibc 2.36, the oldest the asset supports), so the binary runs on any
# distribution with glibc 2.36 or newer: Debian 12, Ubuntu 24.04, Fedora 37,
# RHEL 10, Arch and CachyOS, and later. QEMU is configured with
# --without-default-features and only the features vmlab's command line
# needs are enabled (docs/evidence/portable/features.md maps each vmlab
# feature to its QEMU option and configure switch): KVM and TCG, the
# egl-headless display (OpenGL through epoxy and gbm), VNC, virtio-gpu
# Venus (virglrenderer), the TPM emulator back end, USB host passthrough
# (libusb), vhost-user (virtiofs), qcow2 with zlib and zstd clusters. The
# result links about a dozen shared libraries with long-stable sonames
# (glib, pixman, epoxy, gbm, zlib, zstd, libusb, glibc), which the target
# host provides, and libvirglrenderer.so.1, which the asset bundles in lib/:
# out/host/virglrenderer/ is the one virglrenderer 1.3.0 build for both host
# assets (build-virglrenderer.sh, run first when it is missing), and
# RUNPATH=$ORIGIN/../lib makes the binary load that copy. The graphics stack
# (epoxy, gbm, the Vulkan loader and the GPU drivers) must match the host's
# driver, so it always comes from the host; so does everything else.
#
# The binary is configured with --prefix=/usr, so it uses the host's
# /usr/share/qemu ROMs (through the out/host/qemu/share/qemu symlink here;
# vmlab passes -L with the host QEMU's data directory). A lab uses this
# build when the host config names it as its GPU's qemu_binary (vmlab >=
# 0.26; docs/nvidia.md, "Host setup").
#
# The binary targets a portable CPU baseline, x86-64-v2 (SSE4.2, POPCNT;
# every x86-64 CPU since about 2009), tuned generically, whatever the build
# host is. QEMU itself needs nothing above v2: it picks its AVX2 and AVX-512
# paths (util/bufferiszero.c and the like) at run time from CPUID.
# --x86-version=2 is QEMU's own switch for that level (it adds -mpopcnt,
# -msse4.2 and -mneeded); -march=x86-64-v2 says the same to the compiler,
# and -z x86-64-v2 stamps the level in the .note.gnu.property. Debian's
# start files carry only the baseline note, so the build no longer has to
# strip a CachyOS x86-64-v4 note from them (docs/release.md, "CPU and
# library baseline"). The script fails if the result needs more than v2.
#
# For packaging (scripts/release.sh) it also writes out/host/qemu/BUILDINFO,
# the source commit, the build image and the Debian packages the binary was
# built against, and copies QEMU's licence files to out/host/qemu/licenses/.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/out/host/qemu
WORK=$REPO/.scratch/build-qemu
VIRGL=$REPO/out/host/virglrenderer
read -r URL BRANCH COMMIT < <(awk '$1 == "qemu" && $3 == "branch" && $5 == "commit" { print $2, $4, $6 }' "$HERE/PINS")
URL=${QEMU_URL:-$URL}; COMMIT=${QEMU_COMMIT:-$COMMIT}
[[ ${COMMIT:-} =~ ^[0-9a-f]{40}$ ]] || { echo "no qemu branch/commit pin in $HERE/PINS" >&2; exit 1; }
# shellcheck source=container.sh
. "$HERE/container.sh"

ISA=x86-64-v2
OPTFLAGS="-march=$ISA -mtune=generic"
LINKFLAGS="-Wl,-z,$ISA"

if [ -z "${WIN3D_IN_HOST_BUILD:-}" ]; then
    # On the host: the one virglrenderer build, the fetch, then the container.
    if [ ! -f "$VIRGL/dev/usr/lib/pkgconfig/virglrenderer.pc" ] || [ ! -f "$VIRGL/lib/libvirglrenderer.so.1" ]; then
        echo "no virglrenderer build in $VIRGL: running build-virglrenderer.sh first"
        WITH_WORKAROUND=0 "$HERE/build-virglrenderer.sh"
    fi
    [ ! -e "$WORK" ] || find "$WORK" -delete
    [ ! -e "$OUT" ] || find "$OUT" -delete
    mkdir -p "$WORK/qemu" "$OUT/bin" "$OUT/lib"
    cd "$WORK/qemu"
    echo "fetching $URL $COMMIT (branch $BRANCH)"
    git init -q
    git fetch -q --depth 32 "$URL" "$COMMIT"
    git -c advice.detachedHead=false checkout -q FETCH_HEAD
    [ "$(git rev-parse HEAD)" = "$COMMIT" ] || { echo "fetched $(git rev-parse HEAD), wanted $COMMIT" >&2; exit 1; }
    cd "$REPO"
    in_container "$0" "$@"
    cat "$OUT/BUILDINFO"
    exit 0
fi

# In the container from here on.
cd "$WORK/qemu"
# The series is the run of non-merge commits on top of the upstream merge
# the branch is rebased on; the fetch depth above covers it.
# (awk reads to the end, so that git log gets no SIGPIPE under pipefail.)
SERIES=$(git log --first-parent --abbrev=10 --format='%h %p%x09%s' HEAD | awk -F'\t' '!done { if (split($1, f, " ") > 2) done = 1; else print f[1], $2 }')
BASE=$(git log --first-parent --abbrev=10 --format='%h %p' HEAD | awk '!done && NF > 2 { print $1; done = 1 }')
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
export PKG_CONFIG_PATH=$VIRGL/dev/usr/lib/pkgconfig
# The shallow clone has no tags for git describe, so name the commit instead.
# --without-default-features turns every "auto" feature off; the --enable
# switches below are the set docs/evidence/portable/features.md derives from
# vmlab's command line, plus the library-free ones worth having (TCG for
# accel = "tcg", HMP, malloc-trim, the stack protector). Everything else,
# including every audio, network and block back end that would pull in a
# library, stays off.
./configure --prefix=/usr --target-list=x86_64-softmmu \
    --with-pkgversion="vmlab ${COMMIT:0:12}" \
    --without-default-features \
    --enable-system --disable-user --disable-tools --disable-guest-agent --disable-docs \
    --enable-kvm --enable-tcg \
    --enable-pixman --enable-opengl --enable-virglrenderer --enable-vnc \
    --enable-tpm --enable-libusb --enable-vhost-user --enable-zstd \
    --enable-hmp --enable-malloc-trim --enable-stack-protector \
    --disable-werror --x86-version=2 \
    --extra-cflags="$OPTFLAGS" --extra-cxxflags="$OPTFLAGS" --extra-ldflags="$LINKFLAGS" \
    >"$WORK/configure.log"
grep -E "^ *(GBM|VirGL support|OpenGL support|KVM support|TCG support|VNC support|TPM backend|libusb|vhost-user$|zstd|HMP|malloc trim|AVX2|AVX512BW|OpenGL)" "$WORK/configure.log" | sed 's/^ */  /'
make -j"$(nproc)" qemu-system-x86_64 >"$WORK/make.log" 2>&1 || { tail -40 "$WORK/make.log" >&2; exit 1; }
BIN=build/qemu-system-x86_64
# Check the ISA level the binary asks ld.so for: x86-64-baseline and v2 only.
NEEDED_ISA=$(readelf -n "$BIN" | sed -n 's/^.*x86 ISA needed: //p')
echo "x86 ISA needed: ${NEEDED_ISA:-none}"
case ", $NEEDED_ISA," in
    *x86-64-v3*|*x86-64-v4*|", ,") echo "error: qemu-system-x86_64 needs ISA '${NEEDED_ISA:-none}', wanted $ISA" >&2; exit 1 ;;
esac
cp "$BIN" "$OUT/bin/"
# The build directory's rpath points at out/host/virglrenderer/dev; the asset
# loads lib/libvirglrenderer.so.1 next to bin/ instead.
patchelf --set-rpath '$ORIGIN/../lib' "$OUT/bin/qemu-system-x86_64"
readelf -d "$OUT/bin/qemu-system-x86_64" | grep -q 'RUNPATH.*\$ORIGIN/../lib' || { echo "error: RUNPATH is not \$ORIGIN/../lib" >&2; readelf -d "$OUT/bin/qemu-system-x86_64" | grep -i path >&2; exit 1; }
cp "$VIRGL/lib/libvirglrenderer.so.1" "$OUT/lib/"
# QEMU looks for its ROMs in ../share/qemu next to the binary: use the host's.
mkdir -p "$OUT/share"; ln -sfn /usr/share/qemu "$OUT/share/qemu"
"$OUT/bin/qemu-system-x86_64" --version | head -1
resolved=$(ldd "$OUT/bin/qemu-system-x86_64" | awk '$1 == "libvirglrenderer.so.1" { print $3 }')
[ "$(readlink -f "${resolved:-/nonexistent}")" = "$(readlink -f "$OUT/lib/libvirglrenderer.so.1")" ] \
    || { echo "error: libvirglrenderer.so.1 resolves to '${resolved:-nothing}', not $OUT/lib/libvirglrenderer.so.1" >&2; exit 1; }
echo "libvirglrenderer.so.1 => $resolved"

mkdir -p "$OUT/licenses"; cp COPYING COPYING.LIB LICENSE "$OUT/licenses/"
pkgver() { awk -v p="$1" '$1 == p { print $2 }' /opt/apt-packages.txt; }
PRETTY_NAME=$(sed -n 's/^PRETTY_NAME="\(.*\)"/\1/p' /etc/os-release)
{
    echo "qemu:           $("$OUT/bin/qemu-system-x86_64" --version | head -1)"
    echo "source:         $URL branch $BRANCH"
    echo "commit:         $COMMIT"
    echo "base:           ${BASE:-unknown} (upstream)"
    printf '%s\n' "$SERIES" | sed 's/^/series:         /'
    echo "build host:     ${PRETTY_NAME:-unknown} container, $(uname -m), built $(date -u +%Y-%m-%d)"
    echo "build image:    ${HOST_BUILD_IMAGE:-?} (${HOST_BUILD_IMAGE_ID:-?}), base debian@$(awk '$1 == "debian-bookworm" { print $4 }' "$HERE/PINS")"
    echo "compiler:       $(cc --version | head -1)"
    echo "compiler flags: $OPTFLAGS (link: -z $ISA)"
    echo "configure:      --without-default-features and the --enable switches in patches/host/build-qemu.sh (docs/evidence/portable/features.md)"
    echo "needs ISA:      $NEEDED_ISA"
    echo "glibc:          $(ldd --version | head -1 | awk '{print $NF}') (Debian $(pkgver libc6))"
    echo "needs GLIBC:    $(objdump -T "$OUT/bin/qemu-system-x86_64" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
    for m in glib-2.0:libglib2.0-dev pixman-1:libpixman-1-dev epoxy:libepoxy-dev gbm:libgbm-dev zlib:zlib1g-dev libzstd:libzstd-dev libusb-1.0:libusb-1.0-0-dev; do
        printf 'lib %-11s %s (Debian %s)\n' "${m%%:*}:" "$(pkg-config --modversion "${m%%:*}" 2>/dev/null || echo '?')" "$(pkgver "${m#*:}")"
    done
    echo "lib virglrenderer: $(pkg-config --modversion virglrenderer) (bundled: lib/libvirglrenderer.so.1 from out/host/virglrenderer, $(sed -n 's/^patch: *\([^ ]*\).*/\1/p' "$VIRGL/BUILDINFO" | tr '\n' ' '))"
    echo "RUNPATH:        \$ORIGIN/../lib"
    echo "NEEDED:         $(objdump -p "$OUT/bin/qemu-system-x86_64" | awk '$1 == "NEEDED" { print $2 }' | tr '\n' ' ')"
} >"$OUT/BUILDINFO"
