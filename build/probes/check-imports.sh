#!/usr/bin/env bash
# Fails when a probe imports anything other than Windows system DLLs, so no
# probe can carry an app-local graphics DLL. Runs inside the build image.
set -euo pipefail
out=${1:?usage: check-imports.sh <out/probes>}
allowed='^(kernel32|user32|gdi32|d3d9|d3d11|d3d12|dxgi|opengl32|api-ms-win-crt-[a-z0-9-]+)\.dll$'
status=0
for exe in "$out"/x64/*.exe "$out"/x86/*.exe; do
  while read -r dll; do
    if ! printf '%s\n' "$dll" | tr 'A-Z' 'a-z' | grep -Eq "$allowed"; then
      echo "check-imports: $exe imports $dll, which is not a system DLL" >&2
      status=1
    fi
  done < <(llvm-objdump -p "$exe" | awk '/DLL Name:/ {print $3}')
done
[ "$status" = 0 ] && echo "check-imports: every probe imports only system DLLs"
exit "$status"
