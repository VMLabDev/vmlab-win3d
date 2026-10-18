#!/usr/bin/env bash
# Build the n5 diagnostics.
#
#   docs/evidence/n5/src/build.sh
#
# Guest probes (x64) in the project toolchain image (build/toolchain,
# llvm-mingw 20261006) into .scratch/n5/guest/:
#   vk_rtt.exe <n> <out> [pause-ms]  Venus submit + fence round trip
#   d3d11_rtt.exe <n> <out>          D3D11 clear + Flush + EVENT query round trip
# vk_rtt.c needs the Vulkan headers; it takes them from the Mesa source tree
# that build/mesa/build.sh leaves in out/mesa/.work/src/include.
#
# Host tools with the host's own gcc and its Vulkan, EGL, GL and GBM headers
# (Arch: vulkan-headers, mesa, libglvnd) into .scratch/n5/host/:
#   vkfence [device-index]   vkWaitForFences wake-up latency; EXPORT=1 makes
#                            the fence SYNC_FD-exportable as virglrenderer does
#   glfence <render-node>    GL fence latency the way vrend uses it
#   shim.so                  LD_PRELOAD for QEMU: logs virglrenderer fences
#                            and callbacks to $N5_SHIM_LOG (needs the
#                            virglrenderer 1.3.0 headers: VIRGL_SRC=<tree>)
#   layer/                   Vulkan implicit layer that strips SYNC_FD export
#                            from vkCreateFence (VK_ADD_IMPLICIT_LAYER_PATH)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
out=$repo/.scratch/n5
mkdir -p "$out/guest" "$out/host/layer"

image=$("$repo/build/toolchain/build.sh")
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/w" -w /w "$image" bash -c '
  set -e
  s=docs/evidence/n5/src o=.scratch/n5/guest
  x86_64-w64-mingw32-clang -O2 -static -Iout/mesa/.work/src/include -o $o/vk_rtt.exe $s/vk_rtt.c
  x86_64-w64-mingw32-clang++ -O2 -static -o $o/d3d11_rtt.exe $s/d3d11_rtt.cpp -ld3d11'

gcc -O2 -o "$out/host/vkfence" "$here/vkfence.c" -lvulkan -lpthread
gcc -O2 -o "$out/host/glfence" "$here/glfence.c" -lEGL -lGL -lgbm -lpthread
gcc -O2 -shared -fPIC -o "$out/host/layer/libVkLayer_n5_plain_fence.so" "$here/layer.c"
cat >"$out/host/layer/VkLayer_n5_plain_fence.json" <<JSON
{ "file_format_version": "1.1.2", "layer": { "name": "VK_LAYER_N5_plain_fence", "type": "GLOBAL",
  "library_path": "$out/host/layer/libVkLayer_n5_plain_fence.so", "api_version": "1.3.0",
  "implementation_version": "1", "description": "n5 diagnostic: fences without SYNC_FD export",
  "functions": { "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion" },
  "disable_environment": { "N5_PLAIN_FENCE_DISABLE": "1" } } }
JSON
if [ -n "${VIRGL_SRC:-}" ]; then
    printf '#define VIRGL_MAJOR_VERSION 1\n#define VIRGL_MINOR_VERSION 3\n#define VIRGL_MICRO_VERSION 0\n' >"$out/host/virgl-version.h"
    gcc -O2 -shared -fPIC -I"$out/host" -I"$VIRGL_SRC/src" -o "$out/host/shim.so" "$here/shim.c" -ldl -lpthread
fi
ls -l "$out/guest" "$out/host"
