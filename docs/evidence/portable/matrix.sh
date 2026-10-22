#!/usr/bin/env bash
# Distribution matrix for the two host release assets (docs/release.md,
# "Supported hosts of the host assets"; the result of each run is a matrix.md
# under docs/evidence/, next to the logs).
#
#   docs/evidence/portable/matrix.sh <dir with the two tarballs> [<out dir>] [<image>...]
#
# For each distribution image (pinned by digest in the log), a throwaway
# container installs only the distribution's runtime packages for the
# binaries' NEEDED list (plus zstd to unpack, and awk where the image lacks
# it: install.sh uses awk, sed and grep), confirms against the
# distribution's package index that the QEMU package install.sh names for it
# exists, then from the extracted tarballs runs install.sh --check for both
# assets, installs both into /tmp/prefix, runs qemu-system-x86_64 --version,
# and boots a q35 machine under TCG with -nodefaults -S -qmp stdio, answering
# qmp_capabilities and quit, with no -L: the firmware comes from
# /tmp/prefix/qemu/share/qemu, the link install.sh makes to /usr/share/qemu.
# The containers have no QEMU package, so the host's /usr/share/qemu is
# mounted read-only at /usr/share/qemu and stands in for it. An image name
# with the suffix "+noroms" runs without that mount; there install.sh --check
# must refuse with the message that names the package to install.
# ubuntu:22.04 (glibc 2.35) is expected to be refused by the glibc floor;
# its row records the message.
#
# Writes <out dir>/<image>.log and <out dir>/summary.txt (one line per image:
# image, digest, PASS or FAIL or REFUSED, reason). Docker runs without sudo.
set -uo pipefail
TARBALLS=$(cd "${1:?tarball dir}" && pwd)
OUT=${2:-$(dirname "$0")/matrix}
shift; shift 2>/dev/null || true
IMAGES=("$@")
[ ${#IMAGES[@]} -gt 0 ] || IMAGES=(debian:bookworm debian:trixie ubuntu:24.04 fedora:latest archlinux:latest opensuse/tumbleweed ubuntu:22.04 debian:bookworm+noroms)
mkdir -p "$OUT"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
QT=$(ls "$TARBALLS"/qemu-vmlab-egl-*-x86_64-linux.tar.zst | head -1)
VT=$(ls "$TARBALLS"/virglrenderer-vmlab-*-x86_64-linux.tar.zst | head -1)
[ -f "$QT" ] && [ -f "$VT" ] || { echo "no tarballs in $TARBALLS" >&2; exit 2; }
[ -f /usr/share/qemu/bios-256k.bin ] || { echo "this host has no /usr/share/qemu/bios-256k.bin to lend the containers" >&2; exit 2; }

# The script each container runs. PKGS is the package-install command line,
# QPKG the query that shows the distribution's QEMU package in its index.
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
echo "## the distribution's QEMU package, in its index ($QPKG):"
sh -c "$QPKG" 2>&1 | sed 's/^/   /' || echo "   (query failed)"
echo "## /usr/share/qemu in this container: $(ls /usr/share/qemu/bios-256k.bin 2>/dev/null || echo "no bios-256k.bin") $([ -n "${NOROMS:-}" ] && echo "(the +noroms row: nothing mounted)" || echo "(the host's, mounted read-only)")"
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
    if printf '%s\n' "$qmsg" | grep -q "bios-256k.bin is missing"; then
        echo "## qemu install.sh /tmp/prefix/qemu must refuse the same way:"
        "$Q/install.sh" /tmp/prefix/qemu 2>&1 | sed 's/^/   /'; irc=${PIPESTATUS[0]}
        echo "   exit $irc"
        [ ! -e /tmp/prefix/qemu/bin/qemu-system-x86_64 ] || { echo "RESULT FAIL install went ahead without firmware"; exit 1; }
        [ "$irc" != 0 ] || { echo "RESULT FAIL install exit 0 without firmware"; exit 1; }
        echo "RESULT REFUSED no firmware package (qemu check exit $qrc, install exit $irc, virgl check exit $vrc)"
        exit 0
    fi
    echo "RESULT FAIL install.sh --check (qemu $qrc, virgl $vrc)"; exit 1
fi
echo "## install into /tmp/prefix"
"$Q/install.sh" /tmp/prefix/qemu >/tmp/qi.log 2>&1 || { echo "   qemu install failed"; tail -8 /tmp/qi.log; rc=1; }
"$V/install.sh" /tmp/prefix/virgl >/tmp/vi.log 2>&1 || { echo "   virgl install failed"; tail -5 /tmp/vi.log; rc=1; }
grep -E '^(installed|firmware|boot)' /tmp/qi.log /tmp/vi.log | sed 's/^/   /'
echo "## /tmp/prefix/qemu/share/qemu"
ls -ld /tmp/prefix/qemu/share/qemu | sed 's/^/   /'
echo "## --version"
/tmp/prefix/qemu/bin/qemu-system-x86_64 --version 2>&1 | head -1 | sed 's/^/   /' || rc=1
echo "## bundled library after the install"
ldd /tmp/prefix/qemu/bin/qemu-system-x86_64 | awk '$1 == "libvirglrenderer.so.1" { print "   " $0 }'
echo "## QMP: -machine q35 -accel tcg -display none -nodefaults -S -qmp stdio (no -L)"
out=$(printf '{"execute":"qmp_capabilities"}\n{"execute":"quit"}\n' | timeout 60 /tmp/prefix/qemu/bin/qemu-system-x86_64 -machine q35 -accel tcg -display none -nodefaults -S -qmp stdio 2>&1); qmprc=$?
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
# The package install.sh names for the firmware, looked up in the index the
# pkgs_for command refreshed. Shows the package and what it depends on, so
# the log shows where /usr/share/qemu/bios-256k.bin comes from.
qpkg_for() {
    case $1 in
        debian:*|ubuntu:*) echo "apt-cache show qemu-system-x86 | grep -E '^(Package|Version|Depends):' | head -3; apt-cache show qemu-system-data | grep -E '^(Package|Version):' | head -2" ;;
        fedora:*) echo "dnf -q info qemu-system-x86-core | grep -E '^(Name|Version|Release)' | head -3; dnf -q repoquery --requires qemu-system-x86-core | grep -iE 'seabios|ipxe' | head -3" ;;
        archlinux:*) echo "pacman -Si qemu-base qemu-full | grep -E '^(Name|Version|Depends On)'" ;;
        opensuse/*) echo "zypper -n info --requires qemu-x86 | grep -E '^(Name|Version|Requires)|qemu-seabios|qemu-ipxe|qemu-sgabios|qemu-vgabios'" ;;
        *) echo "false" ;;
    esac
}

: >"$OUT/summary.txt"
for spec in "${IMAGES[@]}"; do
    img=${spec%+noroms}
    roms_mount=(-v /usr/share/qemu:/usr/share/qemu:ro); noroms=
    [ "$img" = "$spec" ] || { roms_mount=(); noroms=1; }
    log=$OUT/$(echo "$spec" | tr '/:+' '___').log
    echo "=== $spec"
    docker pull -q "$img" >/dev/null || { echo "$spec  pull failed" | tee -a "$OUT/summary.txt"; continue; }
    digest=$(docker inspect --format '{{index .RepoDigests 0}}' "$img")
    {
        echo "# $spec = $digest"
        echo "# tarballs: $(basename "$QT") $(sha256sum "$QT" | cut -c1-16)..., $(basename "$VT") $(sha256sum "$VT" | cut -c1-16)..."
        echo "# $(date -Is) on $(uname -n), $(uname -r); ${noroms:+no firmware mounted}${noroms:-/usr/share/qemu of the host mounted read-only at /usr/share/qemu}"
        docker run --rm --init -e PKGS="$(pkgs_for "$img")" -e QPKG="$(qpkg_for "$img")" -e NOROMS="$noroms" -e QNAME="$(basename "$QT")" -e VNAME="$(basename "$VT")" \
            -v "$TARBALLS:/tarballs:ro" "${roms_mount[@]}" "$digest" bash -c "$INNER"
        echo "# container exit $?"
    } >"$log" 2>&1
    result=$(grep '^RESULT' "$log" | tail -1 || echo "RESULT FAIL no result")
    printf '%-22s %-90s %s\n' "$spec" "$digest" "${result#RESULT }" | tee -a "$OUT/summary.txt"
done
