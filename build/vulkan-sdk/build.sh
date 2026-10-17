#!/usr/bin/env bash
# Build the Khronos Vulkan loader (vulkan-1.dll) and vulkaninfo.exe for
# x86_64 and i686 Windows inside the shared toolchain image.
# Outputs: out/vulkan-sdk/{x64,x86}/{vulkan-1.dll,vulkaninfo.exe}
# Inputs are pinned in PINS; the in-container steps are in inner.sh.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
image="$("$repo/build/toolchain/build.sh")"

mkdir -p "$repo/out/vulkan-sdk"
# The checkout is mounted at a fixed path so that paths seen by the compiler
# are the same on every machine.
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --volume "$repo:/w" \
  --workdir /w \
  "$image" \
  bash /w/build/vulkan-sdk/inner.sh
