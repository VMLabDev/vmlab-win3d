#!/usr/bin/env bash
# Install the vmlab render server (virgl_render_server) from this tarball.
#
#   ./install.sh [PREFIX]    install into PREFIX
#                            (default ~/.local/opt/virglrenderer-vmlab)
#   ./install.sh --check     only check that this host can run the server
#
# Checks first that this host can run the server: that its glibc is at least
# the version the server was built against (2.36, Debian 12; BUILDINFO), that
# ldd finds every shared library the server needs at a new enough version,
# that the CPU meets the x86-64 ISA level the server is marked as needing,
# and that the dynamic loader starts it. It stops with the reason if not.
# Then copies libexec/virgl_render_server to <prefix>/libexec and the notes
# and licence to <prefix>/share/doc/virglrenderer-vmlab, and prints the
# qemu_env line for each NVIDIA GPU block in the vmlab host config. A render
# server that is already running keeps its old file until its VM restarts.
#
# The server speaks virglrenderer 1.3.0's render-server protocol. The vmlab
# QEMU bundles the libvirglrenderer of the same build, so the pair always
# matches; the host's own virglrenderer version does not matter and is only
# reported. WIN3D_ALLOW_OLDER_GLIBC=1 turns the glibc floor into a warning.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/libexec/virgl_render_server
REPO_URL=https://github.com/VMLabDev/vmlab-win3d
WANT_VIRGL=1.3.0

usage() { sed -n '3,21p' "$0" | sed 's/^# \{0,1\}//'; }

CHECK_ONLY=0
case ${1:-} in
    -h|--help) usage; exit 0 ;;
    --check) CHECK_ONLY=1 ;;
    -*) usage >&2; exit 2 ;;
esac
PREFIX=${1:-$HOME/.local/opt/virglrenderer-vmlab}
[ "$CHECK_ONLY" = 0 ] || PREFIX=

[ -x "$BIN" ] || { echo "error: $BIN is missing; run install.sh from the extracted tarball" >&2; exit 1; }
[ "$(uname -m)" = x86_64 ] || { echo "error: this server is built for x86_64 Linux, not $(uname -m)" >&2; exit 1; }
command -v ldd >/dev/null || { echo "error: ldd not found" >&2; exit 1; }
binfo() { sed -n "s/^$1: *//p" "$HERE/BUILDINFO"; }

# The glibc floor, as in the QEMU tarball's install.sh.
build_glibc=$(binfo glibc | awk '{ print $1 }')
host_glibc=$(LC_ALL=C ldd --version 2>/dev/null | head -1 | grep -o '[0-9][0-9.]*$' || true)
if [ -n "$build_glibc" ] && [ -n "$host_glibc" ]; then
    if [ "$(printf '%s\n%s\n' "$build_glibc" "$host_glibc" | sort -V | head -1)" != "$build_glibc" ]; then
        {
            echo "error: this host's glibc is $host_glibc; this server needs glibc $build_glibc or newer."
            echo "It was built on Debian 12 (glibc $build_glibc) and runs on Debian 12, Ubuntu 24.04,"
            echo "Fedora 37, RHEL 10, Arch Linux, CachyOS and later. Ubuntu 22.04 and RHEL 9 are"
            echo "below the floor. Build the server on this host with"
            echo "patches/host/build-virglrenderer.sh --install from $REPO_URL, or set"
            echo "WIN3D_ALLOW_OLDER_GLIBC=1 to try anyway (unsupported)."
        } >&2
        [ "${WIN3D_ALLOW_OLDER_GLIBC:-0}" = 1 ] || exit 1
        echo "warning: continuing on glibc $host_glibc because WIN3D_ALLOW_OLDER_GLIBC=1" >&2
    else
        echo "glibc: $host_glibc (the server needs $build_glibc or newer)"
    fi
fi

# The host's own virglrenderer, for information: QEMU's bundled library, not
# the host's, starts this server.
host_virgl=
if command -v pkg-config >/dev/null; then host_virgl=$(pkg-config --modversion virglrenderer 2>/dev/null || true); fi
if [ -z "$host_virgl" ]; then
    lib=$(ldconfig -p 2>/dev/null | awk '/libvirglrenderer\.so\.1 / { print $NF; exit }')
    [ -z "$lib" ] || host_virgl="library $(readlink -f "$lib" | sed 's/.*\.so\.//') (1.3.0 is 1.11.0)"
fi
echo "host virglrenderer: ${host_virgl:-none found} (not used: the vmlab QEMU bundles its own $WANT_VIRGL, which this server pairs with)"

ldd_out=$(LC_ALL=C ldd "$BIN" 2>&1) || true
missing=$(printf '%s\n' "$ldd_out" | awk '/not found/ { print "  " $1 }')
old=$(printf '%s\n' "$ldd_out" | grep -o "version \`[^']*' not found.*" | sed 's/^/  /' || true)
if [ -n "$missing$old" ]; then
    {
        echo "error: this host cannot run the render server in this tarball."
        [ -z "$missing" ] || { echo "Missing shared libraries:"; echo "$missing"; }
        [ -z "$old" ] || { echo "Libraries too old:"; echo "$old"; }
        echo "The server needs these from the host (BUILDINFO): $(binfo 'NEEDED server')"
        echo "README.txt, \"Which hosts it suits\", names the packages. Install them, or build"
        echo "the server on this host with patches/host/build-virglrenderer.sh from $REPO_URL."
    } >&2
    exit 1
fi
echo "ldd: every shared library the server needs is present and new enough"

# The x86-64 ISA level, as in the QEMU tarball's install.sh.
if command -v readelf >/dev/null; then
    need=$(LC_ALL=C readelf -n "$BIN" | sed -n 's/^.*x86 ISA needed: //p')
else
    need=$(binfo 'needs ISA')
fi
need=$(printf '%s\n' "$need" | grep -o 'x86-64-v[234]' | sort -V | tail -1)
need=${need:-x86-64-baseline}
LDSO=/lib64/ld-linux-x86-64.so.2
if ! LC_ALL=C "$LDSO" --help 2>/dev/null | grep -q 'glibc-hwcaps'; then
    echo "ISA: the server needs $need; $LDSO does not list this CPU's level (glibc older than 2.33), so not checked"
else
    have=$(LC_ALL=C "$LDSO" --help 2>/dev/null | sed -n 's/^ *\(x86-64-v[234]\) (supported.*/\1/p' | sort -V | tail -1)
    have=${have:-x86-64-baseline}
    if [ "$(printf '%s\n%s\n' "$need" "$have" | sort -V | tail -1)" != "$have" ]; then
        {
            echo "error: this CPU cannot run the render server in this tarball."
            echo "It needs x86-64 ISA level $need; this CPU supports up to $have."
            echo "Build it on this host with patches/host/build-virglrenderer.sh from $REPO_URL."
        } >&2
        exit 1
    fi
    echo "ISA: the server needs $need; this CPU supports up to $have"
fi

# Start it with no socket: it loads, then refuses to run. A loader failure
# ("error while loading shared libraries", "CPU ISA level") means it cannot.
run=$(LC_ALL=C "$BIN" 2>&1 </dev/null) || true
if ! printf '%s\n' "$run" | grep -q '^virgl_render_server\['; then
    { echo "error: $BIN did not start on this host:"; printf '%s\n' "$run" | sed 's/^/  /'; } >&2
    exit 1
fi
echo "run: the server starts"
[ "$CHECK_ONLY" = 0 ] || exit 0

case $PREFIX in /*) ;; *) PREFIX=$PWD/$PREFIX ;; esac
DOC=$PREFIX/share/doc/virglrenderer-vmlab
mkdir -p "$PREFIX/libexec" "$DOC"
# Replace by rename, so a running server keeps its file.
cp "$BIN" "$PREFIX/libexec/.virgl_render_server.new"
chmod 755 "$PREFIX/libexec/.virgl_render_server.new"
mv -f "$PREFIX/libexec/.virgl_render_server.new" "$PREFIX/libexec/virgl_render_server"
cp "$HERE/README.txt" "$HERE/BUILDINFO" "$DOC/"
cp -r "$HERE/licenses" "$DOC/"
echo "installed $PREFIX/libexec/virgl_render_server"
cat <<EOF

Add this line to every NVIDIA GPU block in ~/.config/vmlab/config.wcl
(vmlab 0.26.0-alpha or later), next to the qemu_binary of the vmlab QEMU
(whose bundled virglrenderer $WANT_VIRGL starts this server), then restart the
VMs on that GPU:

  qemu_env = { RENDER_SERVER_EXEC_PATH: "$PREFIX/libexec/virgl_render_server" }
EOF
