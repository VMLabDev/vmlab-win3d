#!/usr/bin/env bash
# Build the Yttrium viogpu3d KMD and its user-mode helpers, x64 Release.
#
#   build/kmd/build.sh
#
# Writes out/kmd/:
#   x64/viogpu3d.sys, x64/viogpu3d.pdb, x64/viogpuap.exe, x64/vgpusrv.exe
#   x64/viogpu3d.inf              kernel-only INF (installable on its own)
#   viogpu3d_x64_wow64.inf.template  full INF with the Mesa UMD entries
#   BUILDINFO                     pins and output hashes
#
# Downloads go to $WIN3D_CACHE (default .scratch/cache in the repo) and are
# verified against build/kmd/PINS. The compile runs in Docker with no
# network, as the calling user.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
CACHE=${WIN3D_CACHE:-$REPO/.scratch/cache}
OUT=$REPO/out/kmd
# 100.6.101.58000 is the upstream default; 58100 marks this repo's build so
# it ranks above the upstream release and the codex r10 build (58001).
DRIVER_VERSION=${KMD_DRIVER_VERSION:-100.6.101.58100}
IMAGE=vmlab-win3d/kmd-build:1

pin() { awk -v n="$1" '$1 == n { print $'"$2"' }' "$HERE/PINS"; }

fetch_file() { # fetch_file NAME -> prints path
    local name=$1 url sha path
    url=$(pin "$name" 3); sha=$(pin "$name" 5)
    path=$CACHE/dl/$(basename "$url")
    mkdir -p "$CACHE/dl"
    if [ ! -f "$path" ] || ! echo "$sha  $path" | sha256sum -c --quiet - 2>/dev/null; then
        echo "fetch $url" >&2
        curl -fsSL --retry 3 -o "$path.part" "$url"
        echo "$sha  $path.part" | sha256sum -c --quiet - || { echo "sha256 mismatch: $name" >&2; exit 1; }
        mv "$path.part" "$path"
    fi
    echo "$path"
}

fetch_git() { # fetch_git NAME -> prints the exported tree
    local name=$1 url rev repo tree
    url=$(pin "$name" 3); rev=$(pin "$name" 4)
    repo=$CACHE/git/$name.git
    tree=$CACHE/src/$name-$rev
    if [ ! -d "$tree" ]; then
        [ -d "$repo" ] || git clone -q --bare --filter=blob:none "$url" "$repo"
        git -C "$repo" cat-file -e "$rev^{commit}" 2>/dev/null || git -C "$repo" fetch -q origin "$rev"
        [ "$(git -C "$repo" rev-parse "$rev^{commit}")" = "$rev" ] || { echo "bad rev: $name" >&2; exit 1; }
        rm -rf "$tree.part"; mkdir -p "$tree.part"
        git -C "$repo" archive "$rev" | tar -x -C "$tree.part"
        mv "$tree.part" "$tree"
    fi
    echo "$tree"
}

TOOLCHAIN_TAR=$(fetch_file llvm-mingw)
WDK_PKG=$(fetch_file wdk-x64)
SDK_PKG=$(fetch_file sdk-cpp)
SRC_TREE=$(fetch_git yttrium-virtio-gpu)

docker build -q -t "$IMAGE" "$HERE" >/dev/null

rm -rf "$OUT"
mkdir -p "$OUT" "$CACHE/unpacked"
docker run --rm --network none --user "$(id -u):$(id -g)" \
    -v "$HERE:/build:ro" -v "$REPO/patches/kmd:/patches:ro" \
    -v "$SRC_TREE:/src:ro" -v "$CACHE/dl:/dl:ro" -v "$CACHE/unpacked:/unpacked" \
    -v "$OUT:/out" \
    -e TOOLCHAIN_TAR="/dl/$(basename "$TOOLCHAIN_TAR")" \
    -e WDK_PKG="/dl/$(basename "$WDK_PKG")" -e SDK_PKG="/dl/$(basename "$SDK_PKG")" \
    -e DRIVER_VERSION="$DRIVER_VERSION" \
    "$IMAGE" /build/inner.sh

{
    echo "driver_version $DRIVER_VERSION"
    grep -v '^#' "$HERE/PINS" | grep -v '^\s*$' | awk '{print "pin", $1, $4, $5}'
    for p in "$REPO"/patches/kmd/*.patch; do echo "patch $(basename "$p") $(sha256sum <"$p" | cut -d' ' -f1)"; done
    (cd "$OUT" && find . -type f ! -name BUILDINFO | sort | xargs sha256sum | sed 's/^/sha256 /')
} >"$OUT/BUILDINFO"
cat "$OUT/BUILDINFO"
