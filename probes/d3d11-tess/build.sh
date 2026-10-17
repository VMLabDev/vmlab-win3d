#!/usr/bin/env bash
# Build tess_probe.exe for x64 and x86 in the shared toolchain image,
# into out/probes-extra/{x64,x86}/: not out/probes/, where
# tests/guest/Run-AllProbes.ps1 would run it as one of the *_probe.exe.
# Standalone, so the main probe Makefile is unchanged; see docs/d3d11-apps.md.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
image=$("$repo/build/toolchain/build.sh")
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/src" -w /src/probes/d3d11-tess "$image" bash -c '
  set -e
  for a in x64:x86_64 x86:i686; do
    d=/src/out/probes-extra/${a%%:*}; mkdir -p "$d"
    ${a#*:}-w64-mingw32-clang++ -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
      -std=c++17 -fno-exceptions -fno-rtti -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
      -mconsole -static -s -Wl,--no-insert-timestamp \
      -o "$d/tess_probe.exe" tess_probe.cpp -ld3d11 -ldxgi
  done'
"$repo/build/probes/check-imports.sh" "$repo/out/probes-extra"
