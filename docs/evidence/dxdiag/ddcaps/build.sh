#!/usr/bin/env bash
# Build ddcaps.exe for x64 and x86 in the shared toolchain image, into
# out/ddcaps/{x64,x86}/.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
mkdir -p "$repo/out/ddcaps/x64" "$repo/out/ddcaps/x86"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/src" -w /src "$image" bash -c '
  set -e
  flags="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -static -s -Wl,--no-insert-timestamp"
  x86_64-w64-mingw32-clang $flags -o out/ddcaps/x64/ddcaps.exe docs/evidence/dxdiag/ddcaps/ddcaps.c -lddraw -ldxguid -luser32
  i686-w64-mingw32-clang $flags -o out/ddcaps/x86/ddcaps.exe docs/evidence/dxdiag/ddcaps/ddcaps.c -lddraw -ldxguid -luser32'
ls -l "$repo/out/ddcaps/x64/ddcaps.exe" "$repo/out/ddcaps/x86/ddcaps.exe"
