#!/usr/bin/env bash
# Build fbguest.exe for x64 and x86 in the shared toolchain image
# (build/toolchain, llvm-mingw 20261006), with the Vulkan headers that
# build/probes/build.sh fetches (build/probes/PINS).
# Output: out/nvfb/guest/{x64,x86}/fbguest.exe. See docs/nvfb.md.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../../../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
headers=$(ls -d "$repo"/out/probes/deps/vulkan-headers-*/include 2>/dev/null | head -1)
if [ -z "$headers" ]; then
  "$repo/build/probes/build.sh" >/dev/null
  headers=$(ls -d "$repo"/out/probes/deps/vulkan-headers-*/include | head -1)
fi
mkdir -p "$repo/out/nvfb/guest"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  for p in x64:x86_64 x86:i686; do
    a=${p%%:*} t=${p#*:}
    mkdir -p out/nvfb/guest/$a
    $t-w64-mingw32-clang -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
      -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -I"/w/${0#'"$repo"'/}" \
      -mconsole -static -s -Wl,--no-insert-timestamp \
      -o out/nvfb/guest/$a/fbguest.exe docs/evidence/nvfb/src/guest-fbtest/fbguest.c
  done' "$headers"
ls -l "$repo"/out/nvfb/guest/*/fbguest.exe
