#!/usr/bin/env bash
# Build the fixed-function probes for x64 and x86 in the project toolchain
# image (build/toolchain, llvm-mingw 20261006): d3d9_ffp2.exe from this
# directory and d3d9_ffp.exe from docs/evidence/ffp/src. Output:
# .scratch/ffp2-probes/{x64,x86}/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/.scratch/ffp2-probes"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    mkdir -p .scratch/ffp2-probes/$a
    for s in docs/evidence/ffp2/src/d3d9_ffp2.cpp docs/evidence/ffp/src/d3d9_ffp.cpp; do
      $t-w64-mingw32-clang++ -O2 -Wall -std=c++17 -municode -mwindows -static -s \
        -Wl,--no-insert-timestamp -o .scratch/ffp2-probes/$a/$(basename $s .cpp).exe $s -ld3d9
    done
  done'
ls -l "$repo/.scratch/ffp2-probes"/*/
