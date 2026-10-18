#!/usr/bin/env bash
# Build the Xid 69 bisect probes for x64 and x86 in the project toolchain
# image (build/toolchain, llvm-mingw 20261006). Output: .scratch/xid69-probes/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/.scratch/xid69-probes"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    mkdir -p .scratch/xid69-probes/$a
    for s in docs/evidence/xid69/src/*.cpp; do
      $t-w64-mingw32-clang++ -O2 -Wall -std=c++17 -municode -mwindows -static -s \
        -Wl,--no-insert-timestamp -o .scratch/xid69-probes/$a/$(basename "${s%.cpp}").exe "$s" -ld3d9
    done
  done'
ls -l "$repo/.scratch/xid69-probes"/*/
