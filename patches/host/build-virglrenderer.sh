#!/usr/bin/env bash
# Build the vmlab render server: virglrenderer 1.3.0's virgl_render_server
# plus our patches, natively on the host.
#
#   patches/host/build-virglrenderer.sh            build into out/host/virglrenderer/
#   patches/host/build-virglrenderer.sh --install [PREFIX]
#                                                  build, then install into PREFIX
#                                                  (default ~/.local/opt/virglrenderer-vmlab)
#
# Applies patches/host/virglrenderer/*.patch in order to the 1.3.0 release
# (the host's package version, decision D3):
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
# Only the render server is built. Venus (vkr) runs entirely in it: QEMU's
# libvirglrenderer (the host's package, 1.3.0) forks the server named by
# RENDER_SERVER_EXEC_PATH, else /usr/lib/virgl_render_server, and talks to it
# over the render-server socket protocol (server/render_protocol.h), which is
# the same in both because both are 1.3.0. A lab selects this server with
# qemu_env = { RENDER_SERVER_EXEC_PATH: "<prefix>/libexec/virgl_render_server" }
# on its host GPU block (docs/nvidia.md, "Host setup").
#
# The build is native, against the host's own libdrm, gbm and Vulkan headers,
# like build-qemu.sh, so the server suits hosts with the same libraries. It
# targets the x86-64-v2 baseline with -mtune=generic whatever the build CPU is,
# and links with start files stripped of the build glibc's ISA-level note
# (CachyOS stamps x86-64-v4 there; build-qemu.sh explains why that matters).
# meson and PyYAML come from a uv virtualenv, installed from the pinned files.
# Writes:
#
#   out/host/virglrenderer/libexec/virgl_render_server
#   out/host/virglrenderer/virgl_render_server -> libexec/virgl_render_server
#   out/host/virglrenderer/BUILDINFO
#   out/host/virglrenderer/licenses/COPYING
#
# Pins are in patches/host/PINS.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/out/host/virglrenderer
WORK=$REPO/out/host/.virglrenderer-work
DL=$REPO/.scratch/downloads
pin() { awk -v n="$1" '$1 == n { print $2, $4, $6 }' "$HERE/PINS"; }
read -r URL VERSION SHA < <(pin virglrenderer)

INSTALL=
case ${1:-} in
    --install) INSTALL=${2:-$HOME/.local/opt/virglrenderer-vmlab} ;;
    "") ;;
    *) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
esac

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
tarball=$(fetch "$URL" "$SHA")
read -r MESON_URL MESON_VER MESON_SHA < <(pin meson)
read -r PYYAML_URL PYYAML_VER PYYAML_SHA < <(pin pyyaml)
meson_whl=$(fetch "$MESON_URL" "$MESON_SHA")
pyyaml_src=$(fetch "$PYYAML_URL" "$PYYAML_SHA")

[ ! -e "$WORK" ] || find "$WORK" -delete
[ ! -e "$OUT" ] || find "$OUT" -delete
mkdir -p "$WORK/crt" "$OUT/libexec" "$OUT/licenses"

uv venv -q --seed "$WORK/venv"
VIRTUAL_ENV=$WORK/venv uv pip install -q "$meson_whl" "$pyyaml_src"
export PATH=$WORK/venv/bin:$PATH

ISA=x86-64-v2
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
OPTFLAGS="-march=$ISA -mtune=generic"
for f in Scrt1.o crt1.o; do
    objcopy --remove-section=.note.gnu.property "$(cc -print-file-name="$f")" "$WORK/crt/$f"
done
LINKFLAGS="-B$WORK/crt/ -Wl,-z,$ISA"

tar -C "$WORK" -xf "$tarball"
cd "$WORK/virglrenderer-$VERSION"
patches=("$HERE"/virglrenderer/*.patch)
[ "${WITH_WORKAROUND:-0}" != 1 ] || patches+=("$HERE"/virglrenderer/workaround/*.patch)
for p in "${patches[@]}"; do echo "applying $(basename "$p")"; patch -s -p1 <"$p"; done

meson setup b --prefix=/usr --buildtype=release -Dvenus=true -Drender-server-worker=process \
    -Dc_args="$OPTFLAGS" -Dcpp_args="$OPTFLAGS" -Dc_link_args="$LINKFLAGS" -Dcpp_link_args="$LINKFLAGS" \
    >"$WORK/meson-setup.log"
ninja -C b server/virgl_render_server

BIN=b/server/virgl_render_server
NEEDED_ISA=$(readelf -n "$BIN" | sed -n 's/^.*x86 ISA needed: //p')
echo "x86 ISA needed: ${NEEDED_ISA:-none}"
case ", $NEEDED_ISA," in
    *x86-64-v3*|*x86-64-v4*|", ,") echo "error: virgl_render_server needs ISA '${NEEDED_ISA:-none}', wanted $ISA" >&2; exit 1 ;;
esac
cp "$BIN" "$OUT/libexec/"
ln -s libexec/virgl_render_server "$OUT/virgl_render_server"
cp COPYING "$OUT/licenses/"

# shellcheck disable=SC1091
. /etc/os-release
{
    echo "component:      virgl_render_server (vmlab render server)"
    echo "virglrenderer:  $VERSION $URL"
    echo "sha256:         $SHA"
    for p in "${patches[@]}"; do echo "patch:          ${p#"$HERE"/virglrenderer/} $(sha256sum "$p" | cut -c1-16)"; done
    echo "build host:     ${PRETTY_NAME:-unknown}, $(uname -m), built $(date -u +%Y-%m-%d)"
    echo "compiler:       $(cc --version | head -1)"
    echo "compiler flags: $OPTFLAGS (link: -z $ISA)"
    echo "needs ISA:      $NEEDED_ISA"
    echo "meson:          $MESON_VER, PyYAML $PYYAML_VER"
    echo "glibc:          $(ldd --version | head -1 | awk '{print $NF}')"
    echo "needs GLIBC:    $(objdump -T "$OUT/libexec/virgl_render_server" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
    for m in libdrm gbm vulkan; do
        echo "lib $m: $(pkg-config --modversion "$m" 2>/dev/null || echo '?')"
    done
    echo "host libvirglrenderer: $(pkg-config --modversion virglrenderer 2>/dev/null || echo '?') (must be $VERSION: same render-server protocol)"
} >"$OUT/BUILDINFO"
cat "$OUT/BUILDINFO"

if [ -n "$INSTALL" ]; then
    mkdir -p "$INSTALL/libexec" "$INSTALL/share/doc/virglrenderer-vmlab"
    cp "$OUT/libexec/virgl_render_server" "$INSTALL/libexec/.virgl_render_server.new"
    mv -f "$INSTALL/libexec/.virgl_render_server.new" "$INSTALL/libexec/virgl_render_server"
    cp -r "$OUT/BUILDINFO" "$OUT/licenses" "$INSTALL/share/doc/virglrenderer-vmlab/"
    echo "installed $INSTALL/libexec/virgl_render_server"
    echo "qemu_env = { RENDER_SERVER_EXEC_PATH: \"$INSTALL/libexec/virgl_render_server\" }"
fi
