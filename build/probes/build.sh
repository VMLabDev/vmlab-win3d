#!/usr/bin/env bash
# Build every probe for x64 and x86 in Docker, into out/probes/{x64,x86}/.
# Idempotent: the image is tagged by the hash of its inputs, and make only
# rebuilds what changed. Usage: build/probes/build.sh [make args...]
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)

pin() { awk -v n="$1" '$1 == n { print $'"$2"' }' "$here/PINS"; }

tag=vmlab-win3d-probes-build:$(cat "$here/Dockerfile" "$here/PINS" | sha256sum | cut -c1-16)
if ! docker image inspect "$tag" >/dev/null 2>&1; then
  docker build -t "$tag" \
    --build-arg UBUNTU_DIGEST="$(pin ubuntu-base 4)" \
    --build-arg LLVM_MINGW_URL="$(pin llvm-mingw 2)" \
    --build-arg LLVM_MINGW_SHA256="$(pin llvm-mingw 4)" \
    --build-arg VULKAN_HEADERS_URL="$(pin vulkan-headers 2)" \
    --build-arg VULKAN_HEADERS_SHA256="$(pin vulkan-headers 4)" \
    --build-arg GLSLANG_URL="$(pin glslang 2)" \
    --build-arg GLSLANG_SHA256="$(pin glslang 4)" \
    "$here"
fi

mkdir -p "$repo/out/probes"
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$repo:/src" -w /src/probes \
  -e HOME=/tmp \
  "$tag" bash -c 'make OUT=/src/out/probes BUILD=/src/out/probes/obj "$@" &&
    /src/build/probes/check-imports.sh /src/out/probes' make "$@"
