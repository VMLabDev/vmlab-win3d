#!/usr/bin/env bash
# Build the host-only repro with the host's compiler, glslc and libvulkan.
# Output: .scratch/godot-host/linblend (nothing is installed).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/../../../.." && pwd)/.scratch/godot-host
mkdir -p "$out"
glslc --target-env=vulkan1.3 -mfmt=c -o "$out/vs.inc" "$here/linblend.vert"
glslc --target-env=vulkan1.3 -mfmt=c -o "$out/fs.inc" "$here/linblend.frag"
{ printf 'static const uint32_t vs_spv[] = '; cat "$out/vs.inc"; printf ';\nstatic const uint32_t fs_spv[] = '; cat "$out/fs.inc"; printf ';\n'; } >"$out/linblend_spv.h"
cc -O2 -Wall -I"$out" -o "$out/linblend" "$here/linblend.c" -lvulkan
echo "$out/linblend"
