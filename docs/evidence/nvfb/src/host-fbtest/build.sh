#!/usr/bin/env bash
# Build fbtest for the host, in the pinned Arch image of
# patches/host/build-virglrenderer.sh, for generic x86-64 so it runs on Zeus
# (x86-64-v3) as well as helios. Writes out/nvfb/fbtest.
#
#   docs/evidence/nvfb/src/host-fbtest/build.sh
#   scp out/nvfb/fbtest zeus:~/win3d-nvfb/ && ssh zeus '~/win3d-nvfb/fbtest NVIDIA 200'
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../../.." && pwd)
OUT=$REPO/out/nvfb
IMAGE=archlinux@sha256:3c64b538588416593f69a1aebd2f2ec0f2541b29159f32556fa7def2167147a0
mkdir -p "$OUT"
docker run --rm -u 0 -v "$HERE:/src:ro" -v "$OUT:/out" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$IMAGE" bash -euo pipefail -c '
        pacman -Sy --noconfirm --needed gcc vulkan-headers vulkan-icd-loader >/dev/null
        gcc -O2 -march=x86-64 -mtune=generic -Wall -o /out/fbtest /src/fbtest.c -lvulkan
        chown "$HOST_UID:$HOST_GID" /out/fbtest
    '
ls -l "$OUT/fbtest"
