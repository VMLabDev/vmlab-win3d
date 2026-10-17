#!/usr/bin/env bash
# Build uavtest.exe (x64) in the shared toolchain image into
# out/labs-zeus/uavtest/. See uavtest.cpp.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/out/labs-zeus/uavtest"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/src" -w /src "$image" \
  x86_64-w64-mingw32-clang++ -O2 -static -o out/labs-zeus/uavtest/uavtest.exe \
  labs/zeus/uavtest/uavtest.cpp -ld3d11 -ldxgi -ld3dcompiler
ls -l "$repo/out/labs-zeus/uavtest/uavtest.exe"
