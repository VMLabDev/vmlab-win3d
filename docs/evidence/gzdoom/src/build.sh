#!/usr/bin/env bash
# Build the gzdoom repros for x64 and x86 in the project toolchain image
# (build/toolchain, llvm-mingw 20261006): gzgl.exe, GZDoom 4.14.2's WGL
# start-up (gzgl.c). Output: .scratch/gzdoom-probes/{x64,x86}/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/.scratch/gzdoom-probes"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    mkdir -p .scratch/gzdoom-probes/$a
    $t-w64-mingw32-clang -O2 -Wall -static -s -Wl,--no-insert-timestamp \
      -o .scratch/gzdoom-probes/$a/gzgl.exe docs/evidence/gzdoom/src/gzgl.c -lgdi32 -luser32
  done'
ls -l "$repo/.scratch/gzdoom-probes"/*/
