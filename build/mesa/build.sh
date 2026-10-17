#!/usr/bin/env bash
# Build Mesa (arehnman/virtio-win-mesa, plus patches/mesa) for x86_64 and
# i686 Windows inside the shared toolchain image.
# Outputs: out/mesa/{x64,x86}/ viogpu_d3d10.dll viogpu_wgl.dll
#          libvulkan_virtio.dll virtio_icd.json
# Inputs are pinned in PINS; the in-container steps are in inner.sh.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
image="$("$repo/build/toolchain/build.sh")"

mkdir -p "$repo/out/mesa"
# The checkout is mounted at a fixed path so that paths seen by the compiler
# are the same on every machine.
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --volume "$repo:/w" \
  --workdir /w \
  "$image" \
  bash /w/build/mesa/inner.sh "$@"
