#!/usr/bin/env bash
# Build the unid3d probes for x64 and x86 in the project toolchain image
# (build/toolchain, llvm-mingw 20261006). Output: .scratch/unid3d-probes/{x64,x86}/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/.scratch/unid3d-probes"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    mkdir -p .scratch/unid3d-probes/$a
    $t-w64-mingw32-clang++ -O2 -Wall -std=c++17 -municode -mwindows -static -s \
      -Wl,--no-insert-timestamp -o .scratch/unid3d-probes/$a/d3d9_depthbias.exe \
      docs/evidence/unid3d/src/d3d9_depthbias.cpp -ld3d9
  done'
ls -l "$repo/.scratch/unid3d-probes"/*/
