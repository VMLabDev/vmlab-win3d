#!/usr/bin/env bash
# Reproduce the static analysis behind docs/dd16.md: which Windows component
# decides the display modes a 64-bit DirectDraw program sees.
#
#   analyse.sh [WORKDIR]     (default .scratch/dd16-analysis in the repo)
#
# 1. Copies the Windows binaries out of the local windows-11 vmlab template
#    with guestfish (read-only) and prints their sha256 (compare binaries.txt).
# 2. Downloads their public PDBs from the Microsoft symbol server. Set
#    SYMBOL_PROXY=http://host:port if the direct link resets.
# 3. Decompiles the functions named in docs/dd16.md with Ghidra headless
#    (/opt/ghidra) and Decomp.java, one <binary>.c per binary in WORKDIR.
# Nothing it writes is committed; Microsoft's binaries and PDBs stay local.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
W=${1:-$REPO/.scratch/dd16-analysis}
GHIDRA=${GHIDRA:-/opt/ghidra/support/analyzeHeadless}
mkdir -p "$W"
# Ghidra rejects project paths with a component starting with '.', so its
# projects live in a temporary directory.
P=$(mktemp -d); trap 'rm -rf "$P"' EXIT
disk=$(ls -d "$HOME"/.local/share/vmlab/templates/x86_64/windows-11/*/ | sort -V | tail -1)disk.qcow2

# name|guest path|functions (regexes on the full Ghidra name)
targets=(
  "ddraw64|/Windows/System32/ddraw.dll|^DD_EnumDisplayModes4$,^mySetMode$"
  "user32-x64|/Windows/System32/user32.dll|^EnumDisplaySettingsExW$"
  "user32-x86|/Windows/SysWOW64/user32.dll|^EnumDisplaySettingsExW$"
  "wow64win|/Windows/System32/wow64win.dll|^whNtUserEnumDisplaySettings$"
  "win32kbase|/Windows/System32/win32kbase.sys|^DrvEnumDisplaySettings$,^DrvBuildDevmodeList$,^ldevGetDriverModes$,^PruneMode$"
  "cdd|/Windows/System32/cdd.dll|^DrvGetModes$"
  "d3d9|/Windows/System32/d3d9.dll|^GetDisplayModeList$"
  "dxgkrnl|/Windows/System32/drivers/dxgkrnl.sys|OBTAIN_MODES_ON_SOURCE::operator,VIDPN_MGR::_IsSupportedVidPn$,DmmIsAllSourcesPixelFormatValid"
  "apphelp-x64|/Windows/System32/apphelp.dll|"
  "apphelp-x86|/Windows/SysWOW64/apphelp.dll|"
)
for t in "${targets[@]}"; do
  IFS='|' read -r name path _ <<<"$t"
  mkdir -p "$W/$name"
  [ -f "$W/$name/$(basename "$path")" ] || guestfish --ro -a "$disk" -i copy-out "$path" "$W/$name"
done
(cd "$W" && sha256sum */*.dll */*.sys)

proxy=(); [ -n "${SYMBOL_PROXY:-}" ] && proxy=(-x "$SYMBOL_PROXY")
for t in "${targets[@]}"; do
  IFS='|' read -r name path funcs <<<"$t"
  [ -n "$funcs" ] || continue
  bin=$W/$name/$(basename "$path")
  info=$(llvm-readobj --coff-debug-directory "$bin")
  guid=$(sed -n 's/.*PDBGUID: {\(.*\)}.*/\1/p' <<<"$info" | tr -d -)
  age=$(awk '/PDBAge/ {print $2}' <<<"$info")
  pdb=$(awk '/PDBFileName/ {print $2}' <<<"$info")
  [ -f "$W/$name/$pdb" ] || curl -fsSL "${proxy[@]}" --retry 10 --retry-all-errors \
      -A Microsoft-Symbol-Server/10.0 -o "$W/$name/$pdb" \
      "https://msdl.microsoft.com/download/symbols/$pdb/$guid$age/$pdb"
  rm -f "$W/$name.c"
  mkdir -p "$P/$name"; cp "$bin" "$W/$name/$pdb" "$P/$name/"
  DECOMP=$funcs DECOMP_OUT=$W/$name.c "$GHIDRA" "$P" "proj-$name" -import "$P/$name/$(basename "$path")" -overwrite \
      -scriptPath "$HERE" -postScript Decomp.java >"$W/$name.log" 2>&1
  echo "$name: $(grep -c '^// ====' "$W/$name.c" || true) functions -> $W/$name.c"
done
for a in x64 x86; do
  echo "apphelp-$a DWM8And16Bit exports: $(llvm-objdump -p "$W/apphelp-$a/apphelp.dll" | grep -c DWM8And16Bit || true)"
done
