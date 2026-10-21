#!/usr/bin/env bash
# Build virglrenderer 1.3.0 with our patches, once, for both host release
# assets: the vmlab render server (virgl_render_server) and the
# libvirglrenderer the vmlab QEMU bundles.
#
#   patches/host/build-virglrenderer.sh            build into out/host/virglrenderer/
#   patches/host/build-virglrenderer.sh --install [PREFIX]
#                                                  build, then install the render
#                                                  server into PREFIX (default
#                                                  ~/.local/opt/virglrenderer-vmlab)
#
# Applies patches/host/virglrenderer/*.patch in order to the 1.3.0 release:
#
#   0001  diagnostics only: logs failed vkCreateImage and vkAllocateMemory
#         calls, and every call with VKR_LOG_IMAGES=1;
#   0002  creates vkr's queue-sync fences without SYNC_FD export, which
#         removes a 10 ms floor from every Venus ring fence on NVIDIA's
#         driver (docs/n5.md). This is why NVIDIA hosts run this server.
#
# WITH_WORKAROUND=1 also applies patches/host/virglrenderer/workaround/*.patch,
# the host-side workaround for the NVIDIA image-usage refusal that
# patches/mesa/0020 fixes in the guest; it is kept as documentation of the
# diagnosis (docs/nvidia.md) and never shipped.
#
# Venus (vkr) runs entirely in the render server: QEMU's libvirglrenderer
# forks the server named by RENDER_SERVER_EXEC_PATH, else the path compiled
# into it, and talks to it over the render-server socket protocol
# (server/render_protocol.h), which is private to a virglrenderer version.
# That is why the vmlab QEMU ships this build's libvirglrenderer.so.1 in its
# lib/ instead of using the host's (Debian 12 has 0.10.4, Arch 1.3.0): the
# library and the server are then the same 1.3.0 whatever the host has. A
# lab selects this server with
# qemu_env = { RENDER_SERVER_EXEC_PATH: "<prefix>/libexec/virgl_render_server" }
# on its host GPU block, next to the vmlab QEMU's qemu_binary (docs/nvidia.md,
# "Host setup").
#
# The build runs in the Debian 12 container of patches/host/Dockerfile
# (patches/host/container.sh; glibc 2.36), so the binaries run on any
# distribution with glibc 2.36 or newer (docs/release.md, "Supported hosts of
# the host assets"). The server links only libgbm and glibc and dlopens the
# host's Vulkan loader; the library links libepoxy, libgbm, libdrm and glibc.
# Both target the x86-64-v2 baseline with -mtune=generic whatever the build
# CPU is, with -z x86-64-v2 stamping the level; Debian's start files carry
# only the baseline note, so there is nothing to strip from them. meson and
# PyYAML come from the wheels PINS names, installed into a venv in the
# container. Writes:
#
#   out/host/virglrenderer/libexec/virgl_render_server    the render server asset
#   out/host/virglrenderer/virgl_render_server -> libexec/virgl_render_server
#   out/host/virglrenderer/lib/libvirglrenderer.so.1      bundled by the QEMU asset
#   out/host/virglrenderer/dev/                           the meson install tree
#                                                         (headers, .pc) that
#                                                         build-qemu.sh links against
#   out/host/virglrenderer/BUILDINFO
#   out/host/virglrenderer/licenses/COPYING
#
# Pins are in patches/host/PINS. The container (not the host) needs nothing
# installed; the host needs Docker and curl.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/out/host/virglrenderer
WORK=$REPO/.scratch/build-virglrenderer
DL=$REPO/.scratch/downloads
pin() { awk -v n="$1" '$1 == n { print $2, $4, $6 }' "$HERE/PINS"; }
read -r URL VERSION SHA < <(pin virglrenderer)
read -r MESON_URL MESON_VER MESON_SHA < <(pin meson)
read -r PYYAML_URL PYYAML_VER PYYAML_SHA < <(pin pyyaml)
# shellcheck source=container.sh
. "$HERE/container.sh"

INSTALL=
case ${1:-} in
    --install) INSTALL=${2:-$HOME/.local/opt/virglrenderer-vmlab} ;;
    "") ;;
    *) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
esac

ISA=x86-64-v2
OPTFLAGS="-march=$ISA -mtune=generic"
LINKFLAGS="-Wl,-z,$ISA"
patches=("$HERE"/virglrenderer/*.patch)
[ "${WITH_WORKAROUND:-0}" != 1 ] || patches+=("$HERE"/virglrenderer/workaround/*.patch)

if [ -z "${WIN3D_IN_HOST_BUILD:-}" ]; then
    # On the host: download, clean, then build in the container.
    # fetch <url> <sha256>: download into $DL once, check the hash, print the path.
    fetch() {
        # Runs inside $(...), where set -e does not apply, so every step checks
        # its own status: a failed download must stop the build here, not at tar.
        local f=$DL/${1##*/}
        if [ ! -f "$f" ]; then
            curl -fsSL -o "$f.part" "$1" || { rm -f "$f.part"; echo "error: download failed: $1" >&2; return 1; }
            mv "$f.part" "$f" || return 1
        fi
        echo "$2  $f" | sha256sum -c - >&2 || { echo "error: sha256 mismatch for $f" >&2; return 1; }
        echo "$f"
    }
    mkdir -p "$DL"
    fetch "$URL" "$SHA" >/dev/null
    fetch "$MESON_URL" "$MESON_SHA" >/dev/null
    fetch "$PYYAML_URL" "$PYYAML_SHA" >/dev/null
    [ ! -e "$WORK" ] || find "$WORK" -delete
    [ ! -e "$OUT" ] || find "$OUT" -delete
    mkdir -p "$WORK"
    in_container "$0" "$@"
    cat "$OUT/BUILDINFO"
    if [ -n "$INSTALL" ]; then
        mkdir -p "$INSTALL/libexec" "$INSTALL/share/doc/virglrenderer-vmlab"
        cp "$OUT/libexec/virgl_render_server" "$INSTALL/libexec/.virgl_render_server.new"
        mv -f "$INSTALL/libexec/.virgl_render_server.new" "$INSTALL/libexec/virgl_render_server"
        cp -r "$OUT/BUILDINFO" "$OUT/licenses" "$INSTALL/share/doc/virglrenderer-vmlab/"
        echo "installed $INSTALL/libexec/virgl_render_server"
        echo "qemu_env = { RENDER_SERVER_EXEC_PATH: \"$INSTALL/libexec/virgl_render_server\" }"
    fi
    exit 0
fi

# In the container from here on.
tarball=$DL/${URL##*/}
mkdir -p "$OUT/libexec" "$OUT/lib" "$OUT/licenses"
python3 -m venv "$WORK/venv"
"$WORK/venv/bin/pip" install -q "$DL/${MESON_URL##*/}" "$DL/${PYYAML_URL##*/}"
export PATH=$WORK/venv/bin:$PATH
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

tar -C "$WORK" -xf "$tarball"
cd "$WORK/virglrenderer-$VERSION"
for p in "${patches[@]}"; do echo "applying $(basename "$p")"; patch -s -p1 <"$p"; done

# --prefix=/usr --libexecdir=libexec: the path compiled into the library as
# the default render server, /usr/libexec/virgl_render_server, is where
# Debian and Fedora keep theirs; vmlab always names ours with
# RENDER_SERVER_EXEC_PATH instead. --libdir=lib keeps the install tree
# distribution-neutral. platforms=egl: no GLX, so no X11 dependency.
# vulkan-dload (the default) dlopens the host's loader at run time.
meson setup b --prefix=/usr --libdir=lib --libexecdir=libexec --buildtype=release \
    -Dplatforms=egl -Dvenus=true -Drender-server-worker=process -Dvideo=false -Dtests=false \
    -Dc_args="$OPTFLAGS" -Dcpp_args="$OPTFLAGS" -Dc_link_args="$LINKFLAGS" -Dcpp_link_args="$LINKFLAGS" \
    >"$WORK/meson-setup.log"
grep -E "^ *(Venus|EGL|GBM|GLX|Render server)" "$WORK/meson-setup.log" || true
ninja -C b
DESTDIR=$OUT/dev meson install -C b --quiet
# The .pc says prefix=/usr; make it point at this tree so that build-qemu.sh
# links this library and no system one (the container has none anyway).
sed -i "s|^prefix=.*|prefix=$OUT/dev/usr|" "$OUT/dev/usr/lib/pkgconfig/virglrenderer.pc"

SERVER=b/server/virgl_render_server
LIB=$(readlink -f b/src/libvirglrenderer.so.1)
for f in "$SERVER" "$LIB"; do
    needed_isa=$(readelf -n "$f" | sed -n 's/^.*x86 ISA needed: //p')
    echo "$(basename "$f"): x86 ISA needed: ${needed_isa:-none}"
    case ", $needed_isa," in
        *x86-64-v3*|*x86-64-v4*|", ,") echo "error: $f needs ISA '${needed_isa:-none}', wanted $ISA" >&2; exit 1 ;;
    esac
done
cp "$SERVER" "$OUT/libexec/"
ln -s libexec/virgl_render_server "$OUT/virgl_render_server"
cp "$LIB" "$OUT/lib/libvirglrenderer.so.1"
cp COPYING "$OUT/licenses/"

max_glibc() { objdump -T "$1" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1; }
needed() { objdump -p "$1" | awk '$1 == "NEEDED" { print $2 }' | tr '\n' ' '; }
pkgver() { awk -v p="$1" '$1 == p { print $2 }' /opt/apt-packages.txt; }
PRETTY_NAME=$(sed -n 's/^PRETTY_NAME="\(.*\)"/\1/p' /etc/os-release)
{
    echo "component:      virgl_render_server (vmlab render server) and libvirglrenderer.so.1 (bundled by the vmlab QEMU)"
    echo "virglrenderer:  $VERSION $URL"
    echo "sha256:         $SHA"
    for p in "${patches[@]}"; do echo "patch:          ${p#"$HERE"/virglrenderer/} $(sha256sum "$p" | cut -c1-16)"; done
    echo "build host:     ${PRETTY_NAME:-unknown} container, $(uname -m), built $(date -u +%Y-%m-%d)"
    echo "build image:    ${HOST_BUILD_IMAGE:-?} (${HOST_BUILD_IMAGE_ID:-?}), base debian@$(awk '$1 == "debian-bookworm" { print $4 }' "$HERE/PINS")"
    echo "compiler:       $(cc --version | head -1)"
    echo "compiler flags: $OPTFLAGS (link: -z $ISA)"
    echo "needs ISA:      $(readelf -n "$OUT/libexec/virgl_render_server" | sed -n 's/^.*x86 ISA needed: //p')"
    echo "meson:          $MESON_VER, PyYAML $PYYAML_VER"
    echo "glibc:          $(ldd --version | head -1 | awk '{print $NF}') (Debian $(pkgver libc6))"
    echo "needs GLIBC:    $(max_glibc "$OUT/libexec/virgl_render_server") (server), $(max_glibc "$OUT/lib/libvirglrenderer.so.1") (library)"
    for m in libdrm-dev libgbm-dev libepoxy-dev libvulkan-dev; do
        printf 'lib %-13s %s (Debian %s)\n' "${m%-dev}:" "$(pkg-config --modversion "$(case $m in libdrm-dev) echo libdrm;; libgbm-dev) echo gbm;; libepoxy-dev) echo epoxy;; libvulkan-dev) echo vulkan;; esac)" 2>/dev/null || echo '?')" "$(pkgver "$m")"
    done
    echo "NEEDED server:  $(needed "$OUT/libexec/virgl_render_server")"
    echo "NEEDED library: $(needed "$OUT/lib/libvirglrenderer.so.1")"
    echo "protocol:       render-server protocol of virglrenderer $VERSION; the vmlab QEMU bundles this library, so the pair always matches"
} >"$OUT/BUILDINFO"
