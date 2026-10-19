#!/usr/bin/env bash
# Build the f6va helpers for x64 and x86 in the project toolchain image
# (build/toolchain, llvm-mingw 20261006). Output: .scratch/f6va-probes/{x64,x86}/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/.scratch/f6va-probes"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    mkdir -p .scratch/f6va-probes/$a
    $t-w64-mingw32-clang++ -O2 -Wall -std=c++17 -municode -static -s \
      -Wl,--no-insert-timestamp -o .scratch/f6va-probes/$a/vasample.exe \
      docs/evidence/f6va/src/vasample.cpp -lpsapi
  done'
ls -l "$repo/.scratch/f6va-probes"/*/
