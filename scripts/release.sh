#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build the release assets for one version from a clean checkout.
#
#   scripts/release.sh <version>          for example 0.1.0-alpha
#
# Refuses a dirty tree. Runs scripts/build.sh (every component, then the
# signed package) and patches/host/build-qemu.sh (the QEMU commit pinned in
# patches/host/PINS), then writes
# out/release/<version>/:
#   vmlab-win3d-<version>-windows-x64.zip              guest package + README.txt
#   qemu-11.1.1-vmlab-egl-<version>-x86_64-linux.tar.zst  vmlab QEMU + install.sh
#   RELEASE-NOTES.md
#   SHA256SUMS
#
# <version> must start with installer/VERSION (0.1.0 for 0.1.0-alpha).
#
# Releases should share one test certificate. Set WIN3D_SIGNING_DIR to the
# directory that holds it (test-signing.key, .crt, .cer, SIGN_TIME,
# CERT-INFO.txt; build/signing/make-test-cert.sh). For the build the script
# links <repo>/.signing to it (the build scripts read .signing/), and removes
# the link when it exits; the key is never copied. Without WIN3D_SIGNING_DIR
# the checkout's own .signing/ is used, and created if it is missing.
# WIN3D_CACHE is passed through to the builds that read it. docs/release.md
# has the whole procedure.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO"
HERE=$REPO/scripts/release

usage() { sed -n '3,25p' "$0" | sed 's/^# \{0,1\}//'; }
[ $# -eq 1 ] && [ "$1" != -h ] && [ "$1" != --help ] || { usage >&2; exit 2; }
VERSION=$1
[[ $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || { echo "error: version '$VERSION' is not like 0.1.0-alpha" >&2; exit 2; }
PKG_VERSION=$(cat installer/VERSION)
[ "${VERSION%%-*}" = "$PKG_VERSION" ] || { echo "error: version $VERSION does not match installer/VERSION ($PKG_VERSION)" >&2; exit 2; }

stage() { printf '\n==== release: %s (%s)\n' "$1" "$(date -Is)"; }
clean_or_die() {
    local dirty
    dirty=$(git status --porcelain --untracked-files=all)
    [ -z "$dirty" ] || { echo "error: the tree is dirty $1:" >&2; echo "$dirty" >&2; exit 1; }
}

stage "check the tree"
clean_or_die "(commit or remove these first)"
COMMIT=$(git rev-parse HEAD)
EPOCH=$(git log -1 --format=%ct)
# The QEMU source is one pin: the "qemu" line of patches/host/PINS, a commit
# on the VMLabDev/qemu fork branch (patches/host/bump-qemu.sh moves it).
read -r QEMU_URL QEMU_BRANCH QEMU_COMMIT < <(awk '$1 == "qemu" && $3 == "branch" && $5 == "commit" { print $2, $4, $6 }' patches/host/PINS)
echo "QEMU: $QEMU_URL $QEMU_BRANCH $QEMU_COMMIT"

LINKED=
cleanup() { [ -z "$LINKED" ] || rm -f "$REPO/.signing"; }
trap cleanup EXIT
if [ -n "${WIN3D_SIGNING_DIR:-}" ]; then
    SIGNING=$(cd "$WIN3D_SIGNING_DIR" && pwd)
    for f in test-signing.key test-signing.crt test-signing.cer SIGN_TIME CERT-INFO.txt; do
        [ -f "$SIGNING/$f" ] || { echo "error: $SIGNING/$f is missing" >&2; exit 1; }
    done
    if [ -L .signing ]; then
        [ "$(readlink -f .signing)" = "$SIGNING" ] || { echo "error: .signing links to $(readlink -f .signing), not $SIGNING" >&2; exit 1; }
    elif [ -e .signing ]; then
        echo "error: .signing exists in $REPO; remove it or unset WIN3D_SIGNING_DIR" >&2; exit 1
    else
        ln -s "$SIGNING" .signing; LINKED=1
    fi
    echo "signing with $SIGNING"
else
    SIGNING=$REPO/.signing
    echo "WIN3D_SIGNING_DIR is not set: signing with $SIGNING (a new key if it is missing)"
fi

stage "build the package (scripts/build.sh)"
scripts/build.sh
stage "build QEMU (patches/host/build-qemu.sh)"
patches/host/build-qemu.sh
clean_or_die "after the build (a build script wrote into the tree)"

OUT=$REPO/out/release/$VERSION
STAGE=$REPO/out/release/.stage-$VERSION
rm -rf "$OUT" "$STAGE"
mkdir -p "$OUT" "$STAGE"
BUNDLE=vmlab-win3d-$PKG_VERSION
CERT_SUBJECT=$(sed -n 's/^subject: *//p' "$SIGNING/CERT-INFO.txt")
CERT_SHA1=$(sed -n 's/^sha1 thumbprint: *//p' "$SIGNING/CERT-INFO.txt")
grep -q "\"$CERT_SHA1\"" "out/package/$BUNDLE/manifest.json" \
    || { echo "error: out/package/$BUNDLE/manifest.json does not name the certificate $CERT_SHA1" >&2; exit 1; }

stage "driver zip"
ZIP=$OUT/vmlab-win3d-$VERSION-windows-x64.zip
mkdir -p "$STAGE/zip"
cp -r "out/package/$BUNDLE" "$STAGE/zip/"
python3 "$HERE/assemble.py" fill "$HERE/windows-README.txt" "$STAGE/zip/README.txt" --crlf \
    VERSION="$VERSION" COMMIT="$COMMIT" BUNDLE="$BUNDLE" CERT_SUBJECT="$CERT_SUBJECT" CERT_SHA1="$CERT_SHA1"
python3 "$HERE/assemble.py" zip "$STAGE/zip" "$ZIP" "$EPOCH"

stage "QEMU tarball"
QNAME=qemu-11.1.1-vmlab-egl-$VERSION
Q=$STAGE/$QNAME
TARBALL=$OUT/$QNAME-x86_64-linux.tar.zst
mkdir -p "$Q/bin"
cp out/host/qemu/bin/qemu-system-x86_64 "$Q/bin/"
cp out/host/qemu/BUILDINFO "$Q/"
cp -r out/host/qemu/licenses "$Q/"
cp "$HERE/qemu-install.sh" "$Q/install.sh"
chmod 755 "$Q/install.sh" "$Q/bin/qemu-system-x86_64"
binfo() { sed -n "s/^$1: *//p" out/host/qemu/BUILDINFO; }
[ "$(binfo commit)" = "$QEMU_COMMIT" ] || { echo "error: out/host/qemu is commit $(binfo commit), PINS says $QEMU_COMMIT" >&2; exit 1; }
python3 "$HERE/assemble.py" fill "$HERE/qemu-README.txt" "$Q/README.txt" \
    VERSION="$VERSION" COMMIT="$COMMIT" \
    QEMU_URL="$QEMU_URL" QEMU_BRANCH="$QEMU_BRANCH" QEMU_COMMIT="$QEMU_COMMIT" \
    QEMU_SERIES="$(binfo series | sed 's/^/  /')" \
    BUILD_HOST="$(binfo 'build host')" \
    BUILDINFO="$(grep -E '^(compiler|glibc|needs GLIBC|lib )' out/host/qemu/BUILDINFO | sed 's/^/  /')"
find "$Q" -exec touch -h -d "@$EPOCH" {} +
tar --sort=name --mtime="@$EPOCH" --owner=0 --group=0 --numeric-owner --format=gnu \
    -C "$STAGE" -cf - "$QNAME" | zstd -19 -T0 -q -o "$TARBALL"

stage "release notes and checksums"
python3 "$HERE/assemble.py" notes "$OUT/RELEASE-NOTES.md" "$VERSION" "$COMMIT" "$EPOCH" \
    "$SIGNING/CERT-INFO.txt" out/host/qemu/BUILDINFO "$ZIP" "$TARBALL"
(cd "$OUT" && sha256sum "$(basename "$ZIP")" "$(basename "$TARBALL")" RELEASE-NOTES.md >SHA256SUMS)
rm -rf "$STAGE"
clean_or_die "after the release (the release wrote into the tree)"

stage done
ls -l "$OUT"
cat "$OUT/SHA256SUMS"
