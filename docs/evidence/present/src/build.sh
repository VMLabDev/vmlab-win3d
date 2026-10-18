#!/usr/bin/env bash
# Build the present repro probes (pace.exe, gl_alpha.exe) for x64 and x86 in
# the project toolchain image (build/toolchain, llvm-mingw 20261006).
# Output: .scratch/present-probes/{x64,x86}/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/.scratch/present-probes"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  src=docs/evidence/present/src
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    o=.scratch/present-probes/$a
    mkdir -p $o
    flags="-O2 -Wall -municode -mwindows -static -s -Wl,--no-insert-timestamp"
    $t-w64-mingw32-clang++ $flags -std=c++17 -o $o/pace.exe $src/pace.cpp -ld3d9 -lddraw -ldxguid -lgdi32
    $t-w64-mingw32-clang $flags -o $o/gl_alpha.exe $src/gl_alpha.c -lopengl32 -lgdi32
  done'
ls -l "$repo/.scratch/present-probes"/*/
