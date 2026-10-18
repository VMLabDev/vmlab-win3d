#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install the patched QEMU from this release tarball, without root.
#
#   ./install.sh [prefix]    install into prefix
#                            (default ~/.local/opt/qemu-11.1.1-vmlab-egl)
#   ./install.sh --check     only check that this host can run the binary
#
# Checks the host's shared libraries with ldd first, and stops with the
# missing or too-old libraries named if the binary cannot run here. Then
# copies bin/qemu-system-x86_64 to <prefix>/bin and the notes and licences
# to <prefix>/share/doc/qemu-vmlab-egl, runs --version, and prints the
# qemu_binary line for a GPU block in the vmlab host config. A QEMU that
# is already running keeps its old binary until its VM restarts.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/bin/qemu-system-x86_64
REPO_URL=https://github.com/VMLabDev/vmlab-win3d

usage() { sed -n '3,14p' "$0" | sed 's/^# \{0,1\}//'; }

CHECK_ONLY=0
case ${1:-} in
    -h|--help) usage; exit 0 ;;
    --check) CHECK_ONLY=1; shift ;;
esac
[ $# -le 1 ] || { usage >&2; exit 2; }
PREFIX=${1:-$HOME/.local/opt/qemu-11.1.1-vmlab-egl}

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
