#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install the vmlab QEMU from this release tarball, without root.
#
#   ./install.sh [prefix]    install into prefix
#                            (default ~/.local/opt/qemu-vmlab-egl)
#   ./install.sh --check     only check that this host can run the binary
#
# Checks first that this host can run the binary: that its glibc is at least
# the version the binary was built against (2.36, Debian 12; BUILDINFO), that
# ldd finds every shared library at a new enough version, that the bundled
# lib/libvirglrenderer.so.1 is the one the binary loads, that the CPU meets
# the x86-64 ISA level the binary is marked as needing, and that --version
# runs. It stops with the reason if not. Then copies bin/qemu-system-x86_64
# and lib/ to <prefix>, the notes and licences to
# <prefix>/share/doc/qemu-vmlab-egl, runs --version, and prints the
# qemu_binary line for a GPU block in the vmlab host config. A QEMU that is
# already running keeps its old binary until its VM restarts.
#
# WIN3D_ALLOW_OLDER_GLIBC=1 turns the glibc floor into a warning, for a try
# on an unsupported host (the binary's own symbols need GLIBC_2.34).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/bin/qemu-system-x86_64
LIB=$HERE/lib/libvirglrenderer.so.1
REPO_URL=https://github.com/VMLabDev/vmlab-win3d

usage() { sed -n '3,21p' "$0" | sed 's/^# \{0,1\}//'; }

CHECK_ONLY=0
case ${1:-} in
    -h|--help) usage; exit 0 ;;
    --check) CHECK_ONLY=1; shift ;;
esac
[ $# -le 1 ] || { usage >&2; exit 2; }
PREFIX=${1:-$HOME/.local/opt/qemu-vmlab-egl}

[ -x "$BIN" ] || { echo "error: $BIN is missing; run install.sh from the extracted tarball" >&2; exit 1; }
[ -f "$LIB" ] || { echo "error: $LIB is missing; run install.sh from the extracted tarball" >&2; exit 1; }
[ "$(uname -m)" = x86_64 ] || { echo "error: this QEMU is built for x86_64 Linux, not $(uname -m)" >&2; exit 1; }
command -v ldd >/dev/null || { echo "error: ldd not found; it ships with glibc" >&2; exit 1; }
binfo() { sed -n "s/^$1: *//p" "$HERE/BUILDINFO"; }

# The glibc floor: the version the binary was built against. A host below it
# is outside the supported set (README.txt, "Which hosts it suits"), even if
# the binary's own symbol versions (needs GLIBC) happen to resolve.
build_glibc=$(binfo glibc | awk '{ print $1 }')
host_glibc=$(LC_ALL=C ldd --version 2>/dev/null | head -1 | grep -o '[0-9][0-9.]*$' || true)
if [ -n "$build_glibc" ] && [ -n "$host_glibc" ]; then
    if [ "$(printf '%s\n%s\n' "$build_glibc" "$host_glibc" | sort -V | head -1)" != "$build_glibc" ]; then
        {
            echo "error: this host's glibc is $host_glibc; this QEMU needs glibc $build_glibc or newer."
            echo "It was built on Debian 12 (glibc $build_glibc) and runs on Debian 12, Ubuntu 24.04,"
            echo "Fedora 37, RHEL 10, Arch Linux, CachyOS and later. Ubuntu 22.04 and RHEL 9 are"
            echo "below the floor. Build QEMU for this host with patches/host/build-qemu.sh from"
            echo "$REPO_URL (docs/release.md), or set WIN3D_ALLOW_OLDER_GLIBC=1 to try anyway"
            echo "(the binary's symbols need $(binfo 'needs GLIBC'); unsupported)."
        } >&2
        [ "${WIN3D_ALLOW_OLDER_GLIBC:-0}" = 1 ] || exit 1
        echo "warning: continuing on glibc $host_glibc because WIN3D_ALLOW_OLDER_GLIBC=1" >&2
    else
        echo "glibc: $host_glibc (the binary needs $build_glibc or newer)"
    fi
fi

# ldd prints "libfoo.so.1 => not found" for a missing library, and
# "<lib>: version `X' not found (required by <file>)" for one too old.
out=$(LC_ALL=C ldd "$BIN" 2>&1) || true
missing=$(printf '%s\n' "$out" | awk '/=> not found/ { print "  " $1 }' | sort -u)
old=$(printf '%s\n' "$out" | sed -n "s/^.*: \([^:]*\): version \`\([^']*\)' not found.*/  \1 (needs \2)/p" | sort -u)
if printf '%s\n' "$out" | grep -q 'not a dynamic executable'; then
    echo "error: ldd cannot read $BIN: $out" >&2; exit 1
fi
if [ -n "$missing$old" ]; then
    {
        echo "error: this host cannot run the QEMU in this tarball."
        [ -z "$missing" ] || { echo "Missing libraries:"; echo "$missing"; }
        [ -z "$old" ] || { echo "Libraries too old:"; echo "$old"; }
        echo
        echo "The binary needs these libraries from the host (BUILDINFO):"
        echo "  $(binfo NEEDED)"
        echo "README.txt, \"Which hosts it suits\", names the package of each on Debian,"
        echo "Ubuntu, Fedora, Arch and openSUSE. Install them with your distribution's"
        echo "packages, or rebuild QEMU on this host with patches/host/build-qemu.sh from"
        echo "$REPO_URL (docs/release.md)."
    } >&2
    exit 1
fi
echo "ldd: every shared library the binary needs is present and new enough"

# The bundled virglrenderer: RUNPATH=$ORIGIN/../lib must make the binary load
# lib/libvirglrenderer.so.1 from this tarball (and after the install, from
# <prefix>/lib), never the host's copy, whose render-server protocol may differ.
resolved=$(printf '%s\n' "$out" | awk '$1 == "libvirglrenderer.so.1" { print $3 }')
if [ "$(readlink -f "${resolved:-/nonexistent}")" != "$(readlink -f "$LIB")" ]; then
    {
        echo "error: the binary resolves libvirglrenderer.so.1 to '${resolved:-nothing}',"
        echo "not to the bundled $LIB. Extract the whole tarball (bin/ and lib/ side by"
        echo "side) and run install.sh from it."
    } >&2
    exit 1
fi
echo "bundled: libvirglrenderer.so.1 => $resolved"

# The x86-64 ISA level: the binary's .note.gnu.property says which levels it
# needs (readelf, else the BUILDINFO line), and ld.so --help (glibc 2.33 and
# later) lists the levels this CPU supports. ld.so refuses a binary whose
# level is higher with "CPU ISA level is lower than required".
if command -v readelf >/dev/null; then
    need=$(LC_ALL=C readelf -n "$BIN" | sed -n 's/^.*x86 ISA needed: //p')
else
    need=$(binfo 'needs ISA')
fi
need=$(printf '%s\n' "$need" | grep -o 'x86-64-v[234]' | sort -V | tail -1)
need=${need:-x86-64-baseline}
LDSO=/lib64/ld-linux-x86-64.so.2
have=$(LC_ALL=C "$LDSO" --help 2>/dev/null | sed -n 's/^ *\(x86-64-v[234]\) (supported.*/\1/p' | sort -V | tail -1) || true
if ! LC_ALL=C "$LDSO" --help 2>/dev/null | grep -q 'glibc-hwcaps'; then
    echo "ISA: the binary needs $need; $LDSO does not list this CPU's level (glibc older than 2.33), so not checked"
else
    have=${have:-x86-64-baseline}
    if [ "$(printf '%s\n%s\n' "$need" "$have" | sort -V | tail -1)" != "$have" ]; then
        {
            echo "error: this CPU cannot run the QEMU in this tarball."
            echo "The binary needs x86-64 ISA level $need; this CPU supports up to $have"
            echo "($LDSO --help lists the levels). Rebuild QEMU on this host with"
            echo "patches/host/build-qemu.sh from $REPO_URL (docs/release.md)."
        } >&2
        exit 1
    fi
    echo "ISA: the binary needs $need; this CPU supports up to $have"
fi

# Last, run it: this catches anything ldd and the ISA check do not.
if ! ver=$(LC_ALL=C "$BIN" --version 2>&1); then
    {
        echo "error: $BIN --version failed on this host:"
        printf '%s\n' "$ver" | sed 's/^/  /'
    } >&2
    exit 1
fi
echo "run: $(printf '%s\n' "$ver" | head -1)"
[ "$CHECK_ONLY" = 0 ] || exit 0

case $PREFIX in /*) ;; *) PREFIX=$PWD/$PREFIX ;; esac
mkdir -p "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/share/doc/qemu-vmlab-egl"
# Copy then rename, so a QEMU already running from the old files is unaffected.
cp "$LIB" "$PREFIX/lib/.libvirglrenderer.so.1.new"
chmod 755 "$PREFIX/lib/.libvirglrenderer.so.1.new"
mv -f "$PREFIX/lib/.libvirglrenderer.so.1.new" "$PREFIX/lib/libvirglrenderer.so.1"
cp "$BIN" "$PREFIX/bin/.qemu-system-x86_64.new"
chmod 755 "$PREFIX/bin/.qemu-system-x86_64.new"
mv -f "$PREFIX/bin/.qemu-system-x86_64.new" "$PREFIX/bin/qemu-system-x86_64"
cp -r "$HERE/README.txt" "$HERE/BUILDINFO" "$HERE/licenses" "$PREFIX/share/doc/qemu-vmlab-egl/"
echo "installed: $PREFIX/bin/qemu-system-x86_64 and $PREFIX/lib/libvirglrenderer.so.1"
installed_lib=$(LC_ALL=C ldd "$PREFIX/bin/qemu-system-x86_64" | awk '$1 == "libvirglrenderer.so.1" { print $3 }')
[ "$(readlink -f "${installed_lib:-/nonexistent}")" = "$(readlink -f "$PREFIX/lib/libvirglrenderer.so.1")" ] \
    || { echo "error: the installed binary resolves libvirglrenderer.so.1 to '${installed_lib:-nothing}', not $PREFIX/lib" >&2; exit 1; }
"$PREFIX/bin/qemu-system-x86_64" --version | head -1

cat <<EOF

Add this line to each NVIDIA GPU block in the vmlab host config,
~/.config/vmlab/config.wcl (vmlab 0.26.0-alpha or later), together with the
qemu_env line the render-server tarball's install.sh prints (this QEMU's
bundled virglrenderer forks that server; the host's own server may speak
another protocol version):

  qemu_binary = "$PREFIX/bin/qemu-system-x86_64"

For example:

  gpu "Nvidia1" {
    address     = "0000:01:00.0"
    qemu_binary = "$PREFIX/bin/qemu-system-x86_64"
    qemu_env    = { RENDER_SERVER_EXEC_PATH: "/home/<you>/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server" }
  }

Then check it with: vmlab validate
EOF
