#!/usr/bin/env bash
# Build the x86d3d12 repro, d3d12_phases.exe (the D3D12 probe with a phase
# trace and a stall watchdog), for x64 and x86 in the project toolchain image
# (build/toolchain, llvm-mingw 20261006), with the probes' flags
# (probes/Makefile). Output: out/x86d3d12/{x64,x86}/d3d12_phases.exe.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/out/x86d3d12/x64" "$repo/out/x86d3d12/x86"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  cflags="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
  ldflags="-municode -mwindows -static -s -Wl,--no-insert-timestamp"
  src=docs/evidence/x86d3d12/src
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:} o=out/x86d3d12/$a
    $t-w64-mingw32-clang $cflags -c -o $o/probe.o probes/common/probe.c
    $t-w64-mingw32-clang++ $cflags -std=c++17 -fno-exceptions -fno-rtti -c -o $o/d3d12_phases.o $src/d3d12_phases.cpp
    $t-w64-mingw32-clang++ $ldflags -o $o/d3d12_phases.exe $o/d3d12_phases.o $o/probe.o -ld3d12 -ldxgi -lpsapi
    rm -f $o/*.o
  done'
sha256sum "$repo"/out/x86d3d12/*/*.exe
