#!/usr/bin/env bash
# Build the shaders2 repro, d3d11_index2.exe, for x64 and x86 in the project
# toolchain image (build/toolchain, llvm-mingw 20261006).
# Output: .scratch/shaders2-probes/{x64,x86}/. See docs/shaders2.md.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/.scratch/shaders2-probes"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    mkdir -p .scratch/shaders2-probes/$a
    $t-w64-mingw32-clang++ -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
      -std=c++17 -fno-exceptions -fno-rtti -municode -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
      -mconsole -static -s -Wl,--no-insert-timestamp \
      -o .scratch/shaders2-probes/$a/d3d11_index2.exe docs/evidence/shaders2/src/d3d11_index2.cpp -ld3d11
  done'
ls -l "$repo/.scratch/shaders2-probes"/*/
