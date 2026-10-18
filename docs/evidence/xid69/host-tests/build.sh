#!/usr/bin/env bash
# Build the host-only repro with the host's compiler, glslc and libvulkan.
# Output: .scratch/xid69-host/lindepth (nothing is installed).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/../../../.." && pwd)/.scratch/xid69-host
mkdir -p "$out"
glslc --target-env=vulkan1.3 -mfmt=c -o "$out/vs.inc" "$here/lindepth.vert"
glslc --target-env=vulkan1.3 -mfmt=c -o "$out/fs.inc" "$here/lindepth.frag"
{ printf 'static const uint32_t vs_spv[] = '; cat "$out/vs.inc"; printf ';\nstatic const uint32_t fs_spv[] = '; cat "$out/fs.inc"; printf ';\n'; } >"$out/lindepth_spv.h"
cc -O2 -Wall -I"$out" -o "$out/lindepth" "$here/lindepth.c" -lvulkan
echo "$out/lindepth"
