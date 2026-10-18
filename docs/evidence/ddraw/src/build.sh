#!/usr/bin/env bash
# Build the DirectDraw repros (ddfull, ddblit) and ddcaps for x64 and x86 in
# the project toolchain image (build/toolchain, llvm-mingw 20261006).
# Output: out/ddraw/{x64,x86}/.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/out/ddraw/x64" "$repo/out/ddraw/x86"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  flags="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -static -s -Wl,--no-insert-timestamp"
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    for s in docs/evidence/ddraw/src/ddfull.c docs/evidence/ddraw/src/ddblit.c docs/evidence/dxdiag/ddcaps/ddcaps.c; do
      $t-w64-mingw32-clang $flags -o out/ddraw/$a/$(basename "${s%.c}").exe "$s" -lddraw -ldxguid -luser32 -lgdi32
    done
  done'
sha256sum "$repo"/out/ddraw/*/*.exe
