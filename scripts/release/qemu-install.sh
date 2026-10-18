#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install the patched QEMU from this release tarball, without root.
#
#   ./install.sh [prefix]    install into prefix
#                            (default ~/.local/opt/qemu-vmlab-egl)
#   ./install.sh --check     only check that this host can run the binary
#
# Checks first that this host can run the binary: that ldd finds every
# shared library at a new enough version, that the CPU meets the x86-64 ISA
# level the binary is marked as needing, and that --version runs. It stops
# with the reason if not. Then copies bin/qemu-system-x86_64 to <prefix>/bin
# and the notes and licences to <prefix>/share/doc/qemu-vmlab-egl, runs
# --version, and prints the qemu_binary line for a GPU block in the vmlab
# host config. A QEMU that is already running keeps its old binary until its
# VM restarts.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/bin/qemu-system-x86_64
REPO_URL=https://github.com/VMLabDev/vmlab-win3d

usage() { sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'; }

CHECK_ONLY=0
case ${1:-} in
    -h|--help) usage; exit 0 ;;
    --check) CHECK_ONLY=1; shift ;;
esac
[ $# -le 1 ] || { usage >&2; exit 2; }
PREFIX=${1:-$HOME/.local/opt/qemu-vmlab-egl}

[ -x "$BIN" ] || { echo "error: $BIN is missing; run install.sh from the extracted tarball" >&2; exit 1; }
[ "$(uname -m)" = x86_64 ] || { echo "error: this QEMU is built for x86_64 Linux, not $(uname -m)" >&2; exit 1; }
command -v ldd >/dev/null || { echo "error: ldd not found; it ships with glibc" >&2; exit 1; }

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
        echo "The binary was built against these host libraries (BUILDINFO):"
        grep -E '^(build host|glibc|needs GLIBC|lib )' "$HERE/BUILDINFO" | sed 's/^/  /'
        echo
        echo "Install or update those libraries with your distribution's packages,"
        echo "or rebuild QEMU on this host with patches/host/build-qemu.sh from"
        echo "$REPO_URL (docs/release.md)."
    } >&2
    exit 1
fi
echo "ldd: every shared library the binary needs is present and new enough"

# The x86-64 ISA level: the binary's .note.gnu.property says which levels it
# needs (readelf, else the BUILDINFO line), and ld.so --help (glibc 2.33 and
# later) lists the levels this CPU supports. ld.so refuses a binary whose
# level is higher with "CPU ISA level is lower than required".
if command -v readelf >/dev/null; then
    need=$(LC_ALL=C readelf -n "$BIN" | sed -n 's/^.*x86 ISA needed: //p')
else
    need=$(sed -n 's/^needs ISA: *//p' "$HERE/BUILDINFO")
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
mkdir -p "$PREFIX/bin" "$PREFIX/share/doc/qemu-vmlab-egl"
# Copy then rename, so a QEMU already running from the old file is unaffected.
cp "$BIN" "$PREFIX/bin/.qemu-system-x86_64.new"
chmod 755 "$PREFIX/bin/.qemu-system-x86_64.new"
mv -f "$PREFIX/bin/.qemu-system-x86_64.new" "$PREFIX/bin/qemu-system-x86_64"
cp -r "$HERE/README.txt" "$HERE/BUILDINFO" "$HERE/licenses" "$PREFIX/share/doc/qemu-vmlab-egl/"
echo "installed: $PREFIX/bin/qemu-system-x86_64"
"$PREFIX/bin/qemu-system-x86_64" --version | head -1

cat <<EOF

Add this line to each NVIDIA GPU block in the vmlab host config,
~/.config/vmlab/config.wcl (vmlab 0.26.0-alpha or later):

  qemu_binary = "$PREFIX/bin/qemu-system-x86_64"

For example:

  gpu "Nvidia1" {
    address     = "0000:01:00.0"
    qemu_binary = "$PREFIX/bin/qemu-system-x86_64"
  }

Then check it with: vmlab validate
EOF
