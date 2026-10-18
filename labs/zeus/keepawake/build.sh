#!/usr/bin/env bash
# Build keepawake natively on the host that runs it (it links the host's
# libEGL, which the GPU driver provides through glvnd). Needs cc and the EGL
# headers (mesa or libglvnd). Writes keepawake next to this script, or to $1.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
cc -O2 -Wall -Wextra -o "${1:-$HERE/keepawake}" "$HERE/keepawake.c" -lEGL
