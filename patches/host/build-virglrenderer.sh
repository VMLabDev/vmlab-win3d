#!/usr/bin/env bash
# Build a patched virglrenderer 1.3.0 for the host, in Docker, for lab use only.
#
#   patches/host/build-virglrenderer.sh
#
# Applies patches/host/virglrenderer/*.patch in order to the 1.3.0 release
# (the host's package version, decision D3). Those patches only add
# diagnostics (vkCreateImage and vkAllocateMemory failures, and every call with
# VKR_LOG_IMAGES=1). WITH_WORKAROUND=1 also applies
# patches/host/virglrenderer/workaround/*.patch, the host-side workaround for
# the NVIDIA image-usage refusal that patches/mesa/0020 fixes in the guest; it
# is kept as documentation of the diagnosis (docs/nvidia.md). Writes:
#
#   out/host/virglrenderer/virgl_render_server
#   out/host/virglrenderer/libvirglrenderer.so.1*
#
# Nothing is installed on the host. A lab uses the render server by putting
# RENDER_SERVER_EXEC_PATH=<abs path>/virgl_render_server in QEMU's environment
# (the vmlab supervisor's); the system libvirglrenderer in QEMU then spawns
# this server instead of /usr/lib/virgl_render_server. Pins are in
# patches/host/PINS.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/out/host/virglrenderer
DL=$REPO/.scratch/downloads
IMAGE=archlinux@sha256:3c64b538588416593f69a1aebd2f2ec0f2541b29159f32556fa7def2167147a0
URL=https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/1.3.0/virglrenderer-1.3.0.tar.gz
SHA=065bc56e89e6f631f96101cd62eba0748e48eb888b434edc86e89d05395e76f3

mkdir -p "$DL" "$OUT"
tarball=$DL/virglrenderer-1.3.0.tar.gz
[ -f "$tarball" ] || curl -fsSL -o "$tarball" "$URL"
echo "$SHA  $tarball" | sha256sum -c -

docker run --rm -u 0 \
    -v "$tarball:/in/src.tar.gz:ro" -v "$HERE/virglrenderer:/in/patches:ro" -v "$OUT:/out" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e WITH_WORKAROUND="${WITH_WORKAROUND:-0}" \
    "$IMAGE" bash -euo pipefail -c '
        pacman -Sy --noconfirm --needed meson ninja gcc pkgconf patch python python-yaml \
            libdrm mesa libepoxy vulkan-headers vulkan-icd-loader >/dev/null
        mkdir /build && cd /build && tar xf /in/src.tar.gz && cd virglrenderer-1.3.0
        patches=(/in/patches/*.patch)
        [ "$WITH_WORKAROUND" != 1 ] || patches+=(/in/patches/workaround/*.patch)
        for p in "${patches[@]}"; do echo "applying $(basename "$p")"; patch -p1 <"$p"; done
        meson setup b --prefix=/usr --buildtype=release -Dvenus=true \
            -Drender-server-worker=process >/dev/null
        ninja -C b
        rm -rf /out/*
        cp -a b/server/virgl_render_server /out/
        cp -P b/src/libvirglrenderer.so b/src/libvirglrenderer.so.1 b/src/libvirglrenderer.so.1.11.0 /out/
        chown -R "$HOST_UID:$HOST_GID" /out
    '
objdump -T "$OUT/virgl_render_server" | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1
ls -l "$OUT"
