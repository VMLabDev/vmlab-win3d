#!/usr/bin/env bash
# Distribution matrix for the two host release assets (docs/release.md,
# "Supported hosts of the host assets"; the result is matrix.md next to this
# script).
#
#   docs/evidence/portable/matrix.sh <dir with the two tarballs> [<out dir>] [<image>...]
#
# For each distribution image (pinned by digest in the log), a throwaway
# container installs only the distribution's runtime packages for the
# binaries' NEEDED list (plus zstd to unpack, and awk where the image lacks
# it: install.sh uses awk, sed and grep), then from the extracted tarballs runs
# install.sh --check for both assets, installs both into /tmp/prefix, runs
# qemu-system-x86_64 --version, and boots a q35 machine under TCG with
# -nodefaults -S -qmp stdio, answering qmp_capabilities and quit. The
# container has no QEMU package, so the host's /usr/share/qemu is mounted
# read-only at /roms and passed with -L, the way vmlab passes the host QEMU's
# data directory. ubuntu:22.04 (glibc 2.35) is expected to be refused by the
# glibc floor; its row records the message.
#
# Writes <out dir>/<image>.log and <out dir>/summary.txt (one line per image:
# image, digest, PASS or FAIL or REFUSED, reason). Docker runs without sudo.
set -uo pipefail
TARBALLS=$(cd "${1:?tarball dir}" && pwd)
OUT=${2:-$(dirname "$0")/matrix}
shift; shift 2>/dev/null || true
IMAGES=("$@")
[ ${#IMAGES[@]} -gt 0 ] || IMAGES=(debian:bookworm debian:trixie ubuntu:24.04 fedora:latest archlinux:latest opensuse/tumbleweed ubuntu:22.04)
mkdir -p "$OUT"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
QT=$(ls "$TARBALLS"/qemu-vmlab-egl-*-x86_64-linux.tar.zst | head -1)
VT=$(ls "$TARBALLS"/virglrenderer-vmlab-*-x86_64-linux.tar.zst | head -1)
[ -f "$QT" ] && [ -f "$VT" ] || { echo "no tarballs in $TARBALLS" >&2; exit 2; }

# The script each container runs. $1 is the package-install command line.
read -r -d '' INNER <<'EOF'
set -uo pipefail
echo "## $(. /etc/os-release; echo "$PRETTY_NAME")  glibc $(ldd --version | head -1 | awk '{print $NF}')"
echo "## packages: $PKGS"
sh -c "$PKGS" >/tmp/pkgs.log 2>&1 || { echo "package install failed:"; tail -20 /tmp/pkgs.log; exit 3; }
echo "## installed runtime libraries:"
for l in libglib-2.0.so.0 libpixman-1.so.0 libepoxy.so.0 libgbm.so.1 libz.so.1 libzstd.so.1 libusb-1.0.so.0; do
    f=$(ls /usr/lib64/$l /usr/lib/x86_64-linux-gnu/$l /usr/lib/$l 2>/dev/null | head -1)
    printf '   %-20s %s\n' "$l" "${f:+$(readlink -f "$f")}"
done
mkdir -p /tmp/x && cd /tmp/x && tar --zstd -xf /tarballs/"$QNAME" && tar --zstd -xf /tarballs/"$VNAME"
Q=/tmp/x/${QNAME%-x86_64-linux.tar.zst}; V=/tmp/x/${VNAME%-x86_64-linux.tar.zst}
rc=0
echo "## qemu install.sh --check"; "$Q/install.sh" --check 2>&1 | sed 's/^/   /'; qrc=${PIPESTATUS[0]}
echo "   exit $qrc"
echo "## virgl install.sh --check"; "$V/install.sh" --check 2>&1 | sed 's/^/   /'; vrc=${PIPESTATUS[0]}
echo "   exit $vrc"
if [ "$qrc" != 0 ] || [ "$vrc" != 0 ]; then
    qmsg=$("$Q/install.sh" --check 2>&1 || true)
    if printf '%s\n' "$qmsg" | grep -q "needs glibc"; then
        echo "RESULT REFUSED glibc floor (qemu check exit $qrc, virgl check exit $vrc)"
        echo "## with WIN3D_ALLOW_OLDER_GLIBC=1 (informational, unsupported):"
        WIN3D_ALLOW_OLDER_GLIBC=1 "$Q/install.sh" --check 2>&1 | tail -3 | sed 's/^/   /'
        exit 0
    fi
    echo "RESULT FAIL install.sh --check (qemu $qrc, virgl $vrc)"; exit 1
fi
echo "## install into /tmp/prefix"
"$Q/install.sh" /tmp/prefix/qemu >/tmp/qi.log 2>&1 || { echo "   qemu install failed"; tail -5 /tmp/qi.log; rc=1; }
"$V/install.sh" /tmp/prefix/virgl >/tmp/vi.log 2>&1 || { echo "   virgl install failed"; tail -5 /tmp/vi.log; rc=1; }
grep -E '^installed' /tmp/qi.log /tmp/vi.log | sed 's/^/   /'
echo "## --version"
/tmp/prefix/qemu/bin/qemu-system-x86_64 --version 2>&1 | head -1 | sed 's/^/   /' || rc=1
echo "## bundled library after the install"
ldd /tmp/prefix/qemu/bin/qemu-system-x86_64 | awk '$1 == "libvirglrenderer.so.1" { print "   " $0 }'
echo "## QMP: -machine q35 -accel tcg -display none -nodefaults -S -qmp stdio (-L /roms)"
out=$(printf '{"execute":"qmp_capabilities"}\n{"execute":"quit"}\n' | timeout 60 /tmp/prefix/qemu/bin/qemu-system-x86_64 -L /roms -machine q35 -accel tcg -display none -nodefaults -S -qmp stdio 2>&1); qmprc=$?
printf '%s\n' "$out" | sed 's/^/   /'
printf '%s\n' "$out" | grep -q '"QMP"' && printf '%s\n' "$out" | grep -q 'host-qmp-quit' && [ $qmprc = 0 ] || { echo "   QMP test failed (exit $qmprc)"; rc=1; }
echo "## render server from /tmp/prefix (what install.sh --check runs)"
r=$(/tmp/prefix/virgl/libexec/virgl_render_server 2>&1 </dev/null); printf '%s\n' "$r" | head -2 | sed 's/^/   /'
printf '%s\n' "$r" | grep -q '^virgl_render_server\[' || { echo "   render server did not start"; rc=1; }
[ $rc = 0 ] && echo "RESULT PASS" || echo "RESULT FAIL see above"
exit $rc
EOF

pkgs_for() {
    case $1 in
        debian:*|ubuntu:*) echo "apt-get update -qq && apt-get install -y -qq --no-install-recommends zstd libpixman-1-0 libepoxy0 libgbm1 zlib1g libzstd1 libusb-1.0-0 && (apt-get install -y -qq --no-install-recommends libglib2.0-0t64 || apt-get install -y -qq --no-install-recommends libglib2.0-0)" ;;
        fedora:*) echo "dnf -y -q install zstd glib2 pixman libepoxy mesa-libgbm libzstd libusb1 && (dnf -y -q install zlib-ng-compat || dnf -y -q install zlib)" ;;
        archlinux:*) echo "pacman -Sy --noconfirm --needed zstd glib2 pixman libepoxy mesa zlib zstd libusb >/dev/null" ;;
        opensuse/*) echo "zypper -n -q install zstd gawk libglib-2_0-0 libpixman-1-0 libepoxy0 libgbm1 libz1 libzstd1 libusb-1_0-0 >/dev/null" ;;
        *) echo "false" ;;
    esac
}

: >"$OUT/summary.txt"
for img in "${IMAGES[@]}"; do
    log=$OUT/$(echo "$img" | tr '/:' '__').log
    echo "=== $img"
    docker pull -q "$img" >/dev/null || { echo "$img  pull failed" | tee -a "$OUT/summary.txt"; continue; }
    digest=$(docker inspect --format '{{index .RepoDigests 0}}' "$img")
    {
        echo "# $img = $digest"
        echo "# tarballs: $(basename "$QT") $(sha256sum "$QT" | cut -c1-16)..., $(basename "$VT") $(sha256sum "$VT" | cut -c1-16)..."
        echo "# $(date -Is) on $(uname -n), $(uname -r)"
        docker run --rm --init -e PKGS="$(pkgs_for "$img")" -e QNAME="$(basename "$QT")" -e VNAME="$(basename "$VT")" \
            -v "$TARBALLS:/tarballs:ro" -v /usr/share/qemu:/roms:ro "$digest" bash -c "$INNER"
        echo "# container exit $?"
    } >"$log" 2>&1
    result=$(grep '^RESULT' "$log" | tail -1 || echo "RESULT FAIL no result")
    printf '%-22s %-90s %s\n' "$img" "$digest" "${result#RESULT }" | tee -a "$OUT/summary.txt"
done
