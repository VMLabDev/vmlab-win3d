#!/usr/bin/env bash
# Forwarding test for the d3d12.dll bridge, run under Wine on the build host.
# Builds forward_test.exe and a fake native d3d12.dll (fake_native.c) for x64
# and x86 in the toolchain image, lays them out as the bridge expects:
#
#   <dir>/forward_test.exe
#   <dir>/d3d12.dll                      out/d3d12/<arch>/d3d12.dll
#   <dir>/vmlab-win3d/d3d12-native.dll   fake_native.dll (no vkd3dcore.dll)
#
# and runs it with Wine. Needs build/d3d12/build.sh to have run, and `wine`.
# Output and logs go to out/d3d12/test/.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="$repo/out/d3d12"
t="$out/test"
command -v wine >/dev/null || { echo "wine not found; skipping"; exit 77; }
mkdir -p "$t"

image="$("$repo/build/toolchain/build.sh")"
docker run --rm --user "$(id -u):$(id -g)" -v "$repo:/repo:ro" -v "$t:/t" "$image" bash -c '
set -e
for arch in x86_64 i686; do
  case $arch in x86_64) d=x64 ;; i686) d=x86 ;; esac
  mkdir -p /t/$d/vmlab-win3d
  $arch-w64-mingw32-clang -O2 -shared -o /t/$d/vmlab-win3d/d3d12-native.dll \
    /repo/build/d3d12/test/fake_native.c /repo/build/d3d12/test/fake_native.def -Wl,--kill-at
  $arch-w64-mingw32-clang -O2 -static -o /t/$d/forward_test.exe /repo/build/d3d12/test/forward_test.c
done'

export WINEPREFIX="$t/wineprefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
fail=0
for d in x64 x86; do
  install -m 0644 "$out/$d/d3d12.dll" "$t/$d/d3d12.dll"
  echo "== $d"
  # The bridge in the exe's directory wins over Wine's builtin d3d12.
  if (cd "$t/$d" && WINEDLLOVERRIDES="$WINEDLLOVERRIDES;d3d12=n" wine forward_test.exe) 2>&1 | tr -d '\r' | tee "$t/$d.log"; then
    :
  else
    fail=1
  fi
done
exit $fail
