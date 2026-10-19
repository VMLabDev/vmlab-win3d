#!/usr/bin/env bash
# Build gltrace's opengl32.dll (x64) in the project toolchain image.
#   docs/evidence/gzdoom/src/gltrace/build.sh <guest-opengl32.dll>
# The guest's own System32\opengl32.dll gives the export list; every export
# gltrace.c does not define is forwarded to oglreal.dll (that same file,
# renamed, installed next to the application). Output: .scratch/gltrace/.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
out=$repo/.scratch/gltrace
mkdir -p "$out"
cp "$1" "$out/oglreal.dll"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  src=docs/evidence/gzdoom/src/gltrace out=.scratch/gltrace
  mine=$(grep -o "WINAPI \(wgl[A-Za-z]*\)(" $src/gltrace.c | sed "s/WINAPI //; s/(//")
  { echo "LIBRARY opengl32.dll"; echo "EXPORTS"
    llvm-readobj --coff-exports $out/oglreal.dll | awk "/Name:/ {print \$2}" | while read -r n; do
      if echo "$mine" | grep -qx "$n"; then echo "  $n"; else echo "  $n = oglreal.$n"; fi
    done; } >$out/gltrace.def
  x86_64-w64-mingw32-clang -O2 -shared -s -o $out/opengl32.dll $src/gltrace.c $out/gltrace.def'
ls -l "$out"
