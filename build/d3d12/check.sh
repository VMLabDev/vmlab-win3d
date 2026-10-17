#!/usr/bin/env bash
# Check the built DLLs in out/d3d12 and record evidence beside them.
#
# usage: check.sh <out/d3d12 directory>
#
# - The bridge must export every name and ordinal of a stock Windows
#   d3d12.dll (native-exports/*.txt) with the documented entry points.
# - Every DLL may import only Windows system DLLs.
# Writes <dir>/{x64,x86}/exports.txt and imports.txt, and <dir>/SHA256SUMS.
set -euo pipefail
out="$1"
here="$(cd "$(dirname "$0")" && pwd)"
objdump=llvm-objdump
command -v "$objdump" >/dev/null || objdump=/opt/llvm-mingw/bin/llvm-objdump

# Windows system DLLs the components may import. api-ms-win-* are the API
# sets (including the UCRT); everything else is in System32 on Windows 10/11.
system_dll='^(api-ms-win-[a-z0-9-]+|kernel32|user32|gdi32|advapi32|ole32|oleaut32|shell32|shlwapi|ntdll|dxgi|dwmapi|setupapi|version|winmm|bcrypt|cfgmgr32)\.dll$'

# Documented D3D12 entry points the bridge must export (brief acceptance).
documented="D3D12CreateDevice D3D12GetDebugInterface D3D12SerializeRootSignature
D3D12SerializeVersionedRootSignature D3D12CreateRootSignatureDeserializer
D3D12CreateVersionedRootSignatureDeserializer D3D12EnableExperimentalFeatures D3D12GetInterface"

# exports <dll>: "ordinal name" lines, "#" for an ordinal-only export.
exports() {
  "$objdump" -p "$1" | awk '/^Export Table:/ { e = 1; next }
    e && /^ *Ordinal +RVA/ { t = 1; next }
    t && NF == 0 { exit }
    t { print $1, (NF >= 3 ? $3 : "#") }'
}

imports() {
  "$objdump" -p "$1" | awk '/DLL Name:/ { print tolower($3) }' | sort -u
}

fail=0
for dir in x64 x86; do
  d="$out/$dir"
  bridge="$d/d3d12.dll"
  exports "$bridge" > "$d/exports.txt"

  for name in $documented; do
    grep -qx "[0-9]* $name" "$d/exports.txt" || { echo "FAIL $dir: bridge lacks $name"; fail=1; }
  done
  # Every export of each recorded native d3d12.dll, by name; ordinal-only
  # exports by ordinal. 101 and 102 are imported by ordinal, so they must match.
  for native in "$here"/native-exports/*-"$dir".txt; do
    while read -r ord name; do
      if [[ "$name" == "#" ]]; then
        grep -qx "$ord #" "$d/exports.txt" || { echo "FAIL $dir: no ordinal-only export $ord ($(basename "$native"))"; fail=1; }
      else
        grep -q " $name\$" "$d/exports.txt" || { echo "FAIL $dir: no export $name ($(basename "$native"))"; fail=1; }
      fi
    done < <(grep -v '^#' "$native")
  done
  for ord_name in "101 D3D12CreateDevice" "102 D3D12GetDebugInterface"; do
    grep -qx "$ord_name" "$d/exports.txt" || { echo "FAIL $dir: $ord_name ordinal mismatch"; fail=1; }
  done

  : > "$d/imports.txt"
  for dll in d3d12.dll vkd3dcore.dll dxgi-vulkan.dll; do
    while read -r imp; do
      echo "$dll $imp" >> "$d/imports.txt"
      [[ "$imp" =~ $system_dll ]] || { echo "FAIL $dir: $dll imports non-system $imp"; fail=1; }
    done < <(imports "$d/$dll")
  done
done

(cd "$out" && sha256sum x64/*.dll x86/*.dll > SHA256SUMS)
if [[ $fail -ne 0 ]]; then
  echo "check: FAILED"
  exit 1
fi
echo "check: bridge exports match the native tables; imports are system DLLs only"
cat "$out/SHA256SUMS"
