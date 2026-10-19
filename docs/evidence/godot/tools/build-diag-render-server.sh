#!/usr/bin/env bash
# Build the diagnostic render server used for the Godot Xid 69 study, in Docker.
#
#   docs/evidence/godot/tools/build-diag-render-server.sh
#
# virglrenderer 1.3.0 (patches/host/PINS) plus patches/host/virglrenderer/*.patch
# plus patches/host/virglrenderer/diagnostic/0040 (VKR_TRACE, VKR_SYNC_SUBMIT;
# docs/godot.md). Same build image and steps as
# patches/host/build-virglrenderer.sh on main at the time of the study. Writes
# .scratch/godot-diag-server/virgl_render_server; nothing is installed. A lab
# uses it through RENDER_SERVER_EXEC_PATH in its host GPU's qemu_env.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
OUT=$REPO/.scratch/godot-diag-server
DL=$REPO/.scratch/downloads
IMAGE=archlinux@sha256:3c64b538588416593f69a1aebd2f2ec0f2541b29159f32556fa7def2167147a0
read -r URL SHA < <(awk '$1 == "virglrenderer" { print $2, $NF }' "$REPO/patches/host/PINS")

mkdir -p "$DL" "$OUT"
tarball=$DL/${URL##*/}
[ -f "$tarball" ] || curl -fsSL -o "$tarball" "$URL"
echo "$SHA  $tarball" | sha256sum -c -

docker run --rm -u 0 \
    -v "$tarball:/in/src.tar.gz:ro" -v "$REPO/patches/host/virglrenderer:/in/patches:ro" -v "$OUT:/out" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    "$IMAGE" bash -euo pipefail -c '
        pacman -Sy --noconfirm --needed meson ninja gcc pkgconf patch python python-yaml \
            libdrm mesa libepoxy vulkan-headers vulkan-icd-loader >/dev/null
        mkdir /build && cd /build && tar xf /in/src.tar.gz && cd virglrenderer-1.3.0
        for p in /in/patches/*.patch /in/patches/diagnostic/*.patch; do
            echo "applying $(basename "$p")"; patch -p1 <"$p"
        done
        meson setup b --prefix=/usr --buildtype=debugoptimized -Dvenus=true \
            -Drender-server-worker=process >/dev/null
        ninja -C b server/virgl_render_server
        cp -a b/server/virgl_render_server /out/
        chown -R "$HOST_UID:$HOST_GID" /out
    '
ls -l "$OUT/virgl_render_server"
