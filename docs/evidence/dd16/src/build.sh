#!/usr/bin/env bash
# Build the 16 bpp repros for x64 and x86 in the project toolchain image
# (build/toolchain, llvm-mingw 20261006): modeprobe (every mode list a
# program sees) and ddfull (docs/evidence/ddraw/src/ddfull.c, the fullscreen
# flip test). Output: out/dd16/{x64,x86}/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/out/dd16/x64" "$repo/out/dd16/x86"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  flags="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -static -s -Wl,--no-insert-timestamp"
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    for s in docs/evidence/dd16/src/modeprobe.c docs/evidence/ddraw/src/ddfull.c; do
      $t-w64-mingw32-clang $flags -o out/dd16/$a/$(basename "${s%.c}").exe "$s" -ld3d9 -lddraw -ldxguid -luser32 -lgdi32
    done
  done'
sha256sum "$repo"/out/dd16/*/*.exe
