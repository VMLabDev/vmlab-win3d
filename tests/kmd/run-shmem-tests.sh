#!/usr/bin/env bash
# Compile the actual pinned allocator with host substitutes for kernel primitives.
# Usage: bash tests/kmd/run-shmem-tests.sh UPSTREAM_SOURCE [--baseline]
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
source_root=${1:?pass the pinned yttrium source directory}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/viogpu/viogpu3d"
cp "$source_root/viogpu/viogpu3d/viogpu_shmem_allocator."{h,cpp} "$work/viogpu/viogpu3d/"
cp "$repo/tests/kmd/shmem-helper.h" "$work/viogpu/viogpu3d/helper.h"
if [[ ${2:-} != --baseline ]]; then
    patch -s -d "$work" -p1 < "$repo/patches/kmd/0005-kmd-size-shmem-allocator-from-region.patch"
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$work/viogpu/viogpu3d" "$repo/tests/kmd/shmem-test.cpp" \
    "$work/viogpu/viogpu3d/viogpu_shmem_allocator.cpp" -o "$work/shmem-test"
"$work/shmem-test"
