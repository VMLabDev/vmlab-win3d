#!/usr/bin/env bash
# Build every probe for x64 and x86 in the shared toolchain image, into
# out/probes/{x64,x86}/. Idempotent: the image and the pinned Vulkan headers
# are reused when present, and make rebuilds only what changed.
# Usage: build/probes/build.sh [make args...]   (for example: clean)
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=$repo/out/probes
image=$("$repo/build/toolchain/build.sh")

pin() { awk -v n="$1" '$1 == n { print $'"$2"' }' "$here/PINS"; }

# Vulkan-Headers: the toolchain image has none.
headers_version=$(pin vulkan-headers 2)
headers=$out/deps/vulkan-headers-${headers_version%%@*}
if [ ! -f "$headers/include/vulkan/vulkan.h" ]; then
  mkdir -p "$out/deps"
  tarball=$out/deps/vulkan-headers.tar.gz
  curl -fsSL -o "$tarball" "$(pin vulkan-headers 4)"
  echo "$(pin vulkan-headers 3)  $tarball" | sha256sum -c --quiet -
  rm -rf "$headers" && mkdir -p "$headers"
  tar -xzf "$tarball" -C "$headers" --strip-components=1
  rm -f "$tarball"
fi

docker run --rm --user "$(id -u):$(id -g)" \
  -v "$repo:/src" -w /src/probes \
  "$image" bash -c 'make OUT=/src/out/probes BUILD=/src/out/probes/obj \
      VULKAN_HEADERS=/src/out/probes/deps/'"$(basename "$headers")"'/include "$@" &&
    case " $* " in *" clean "*) ;; *) /src/build/probes/check-imports.sh /src/out/probes ;; esac' make "$@"
