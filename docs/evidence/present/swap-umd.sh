#!/usr/bin/env bash
# Development aid: replace the installed UMD and WGL DLLs in a kept lab with
# another build, without reinstalling. The proof runs in docs/present.md use
# clean installs instead.
#
#   swap-umd.sh <lab-dir> <dir>
#
# <dir> holds x64/ and x86/ with viogpu_d3d10.dll and viogpu_wgl.dll (for
# example out/mesa). Loaded copies cannot be overwritten, so each installed
# file is renamed to <name>.old-<ticks> first; processes started afterwards
# load the new one (dwm keeps the old one until it restarts).
set -euo pipefail
LABDIR=$(cd "$1" && pwd); SRC=$(cd "$2" && pwd)
VM=win
cd "$LABDIR"
for a in x64 x86; do
    for f in viogpu_d3d10.dll viogpu_wgl.dll; do
        vmlab cp "$SRC/$a/$f" "$VM:C:/win3d/swap/$a/$f" >/dev/null
    done
done
vmlab exec --timeout 120 "$VM" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command '
$t = [DateTime]::Now.Ticks
foreach ($p in @(@("x64", "C:\Windows\System32"), @("x86", "C:\Windows\SysWOW64"))) {
  foreach ($f in "viogpu_d3d10.dll", "viogpu_wgl.dll") {
    $dst = Join-Path $p[1] $f
    if (Test-Path $dst) { Rename-Item $dst "$f.old-$t" }
    Copy-Item (Join-Path "C:\win3d\swap\$($p[0])" $f) $dst
    "{0} {1}" -f (Get-FileHash $dst).Hash.Substring(0, 16), $dst
  }
}' | tr -d '\r'
