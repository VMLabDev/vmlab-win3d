#!/usr/bin/env bash
# Build Microsoft's DirectX-Graphics-Samples D3D12 samples, unmodified, for
# x64 Windows with clang-cl against the MSVC CRT and the Windows SDK, into
# out/d3d12-samples/. Runs in Docker as the calling user; needs no sudo.
#
# usage: build/d3d12-samples/build.sh [--clean]
#   --clean  discard downloads, sources and the splatted CRT/SDK first
#
# Every input is pinned in PINS; the in-container steps are in
# container-build.sh and build-samples.py. The MSVC CRT and Windows SDK are
# fetched by xwin, which needs the Microsoft licence accepted
# (XWIN_ACCEPT_LICENSE); the result stays in out/d3d12-samples/.work and is
# never committed or packaged.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/out/d3d12-samples"

if [[ "${1:-}" == "--clean" ]]; then
  rm -rf "$out"
fi
mkdir -p "$out"

# The shared toolchain image (build/toolchain): clang 23.1.3, lld, Python.
image="$("$repo/build/toolchain/build.sh")"

tty=()
[[ -t 1 ]] && tty=(-t)
docker run --rm "${tty[@]}" \
  --user "$(id -u):$(id -g)" \
  -v "$repo:/repo:ro" \
  -v "$out:/out" \
  -e JOBS="${JOBS:-$(nproc)}" \
  -e TOOLCHAIN_IMAGE="$image" \
  "$image" bash /repo/build/d3d12-samples/container-build.sh
