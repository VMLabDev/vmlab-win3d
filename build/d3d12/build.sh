#!/usr/bin/env bash
# Build the system-wide D3D12 path for x64 and x86 Windows into out/d3d12/:
# vkd3d-proton core (vkd3dcore.dll), the d3d12.dll bridge, and a DXGI-only
# DXVK (dxgi-vulkan.dll). Runs in Docker as the calling user; needs no sudo.
#
# usage: build/d3d12/build.sh [--clean]
#   --clean  discard fetched sources and build trees first
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/out/d3d12"

if [[ "${1:-}" == "--clean" ]]; then
  rm -rf "$out"
fi
mkdir -p "$out"

# The shared toolchain image (build/toolchain): llvm-mingw, Meson, glslang.
image="$("$repo/build/toolchain/build.sh")"

tty=()
[[ -t 1 ]] && tty=(-t)
docker run --rm "${tty[@]}" \
  --user "$(id -u):$(id -g)" \
  -v "$repo:/repo:ro" \
  -v "$out:/out" \
  -e JOBS="${JOBS:-$(nproc)}" \
  -e TOOLCHAIN_IMAGE="$image" \
  "$image" bash /repo/build/d3d12/container-build.sh
