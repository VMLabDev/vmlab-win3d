#!/usr/bin/env bash
# Container side of build/kmd/build.sh. Unpacks the verified toolchain and
# kits, assembles the patched source tree, compiles, and writes /out.
set -euo pipefail

unpack() { # unpack ARCHIVE KIND -> prints dir; cached by archive name
    local a=$1 kind=$2 d=/unpacked/$(basename "$1")
    if [ ! -f "$d/.done" ]; then
        rm -rf "$d"; mkdir -p "$d"
        case $kind in
            tar) tar -xJf "$a" -C "$d" --strip-components=1 ;;
            zip) unzip -q "$a" -d "$d" ;;
        esac
        touch "$d/.done"
    fi
    echo "$d"
}

export TOOLCHAIN WDK SDK
TOOLCHAIN=$(unpack "$TOOLCHAIN_TAR" tar)
WDK=$(unpack "$WDK_PKG" zip)
SDK=$(unpack "$SDK_PKG" zip)

# A fixed work path keeps object and PDB paths identical between builds.
W=/tmp/kmd-work
rm -rf "$W"
export SRC=$W/src OBJ=$W/obj
mkdir -p "$SRC"
cp -a /src/VirtIO /src/viogpu /src/build "$SRC/"
chmod -R u+w "$SRC"
for p in /patches/*.patch; do
    echo "== patch $(basename "$p")"
    patch -d "$SRC" -p1 --no-backup-if-mismatch -s <"$p"
done

# Upstream's commit date, for a reproducible DriverVer.
DRIVER_DATE=09/27/2026
export SOURCE_DATE_EPOCH=$(date -u -d 2026-09-27T00:00:00Z +%s)

OUT=/out/x64 /build/compile-kmd.sh
OUT=/out/x64 /build/compile-helpers.sh

echo "== INF"
python3 /build/stampinf.py "$SRC/viogpu/viogpu3d/viogpu3d_x64_wow64.inx" \
    --version "$DRIVER_VERSION" --date "$DRIVER_DATE" \
    --full /out/viogpu3d_x64_wow64.inf.template --kernel-only /out/x64/viogpu3d.inf
rm -rf "$W"
