#!/usr/bin/env python3
"""Extract the DDK headers Mesa's Windows drivers need into include/winddk.

Usage: prepare-winddk.py <mesa-src> <sdk.nupkg> <wdk.nupkg>

The headers come from the Windows SDK and WDK NuGet packages, version
10.0.26100.1 (Include/10.0.26100.0). Each header is stored under its
lower-case name. When the package spells it differently, a symlink with the
package's spelling points at it, because Mesa includes both spellings.
"""
from pathlib import Path
import shutil
import sys
import zipfile

NAMES = {
    "d3d10tokenizedprogramformat.hpp", "d3d11tokenizedprogramformat.hpp",
    "d3d10umddi.h", "d3dkmddi.h", "d3dkmdt.h", "d3dkmthk.h", "d3dukmdt.h",
    "d3dumddi.h", "dxgiddi.h", "dxgiformat.h", "dxgitype.h", "dxmini.h",
    "wmidata.h",
}

def main(src, *packages):
    dest = Path(src) / "include" / "winddk"
    found = set()
    for package_path in packages:
        with zipfile.ZipFile(package_path) as package:
            for entry in sorted(package.infolist(), key=lambda e: e.filename):
                base = Path(entry.filename).name
                name = base.lower()
                if (name not in NAMES or name in found
                        or "/Include/10.0.26100.0/" not in entry.filename):
                    continue
                target = dest / name
                with package.open(entry) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
                if base != name:
                    link = dest / base
                    if link.is_symlink() or link.exists():
                        link.unlink()
                    link.symlink_to(name)
                found.add(name)
                print(f"{Path(package_path).name}: {entry.filename}")
    missing = sorted(NAMES - found)
    if missing:
        sys.exit(f"error: headers not found: {missing}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
