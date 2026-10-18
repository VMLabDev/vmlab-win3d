#!/usr/bin/env bash
# Build the ExecuteIndirect repro (ei_repro.cpp) for x64 in the project
# toolchain image (build/toolchain, llvm-mingw 20261006). Output: out/ei/x64/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/out/ei/x64"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  x86_64-w64-mingw32-clang++ -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    -std=c++17 -fno-exceptions -fno-rtti -D_WIN32_WINNT=0x0A00 -static -s -Wl,--no-insert-timestamp \
    -o out/ei/x64/ei_repro.exe docs/evidence/ei/src/ei_repro.cpp -ld3d12 -ldxgi -ldxguid'
sha256sum "$repo"/out/ei/x64/*.exe
