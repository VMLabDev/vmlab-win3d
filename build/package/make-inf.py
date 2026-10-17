#!/usr/bin/env python3
"""Write the package INF from out/kmd/viogpu3d_x64_wow64.inf.template.

The template is upstream's full INF after stampinf (build/kmd/stampinf.py).
It lists Mesa files this repo does not build and registry values the package
must not write. This script applies the registration design in
docs/install.md:

  - keep the KMD, helpers, viogpu_d3d10, viogpu_wgl and the Venus ICD with
    its manifest, x64 into %11% and x86 into %16425%;
  - drop libEGL, libGLESv1_CM, libGLESv2 and opengl32 (the package never
    replaces the system opengl32.dll);
  - UserModeDriverName and UserModeDriverNameWow name viogpu_d3d10.dll three
    times (D3D9, D3D10, D3D11) and InstalledDisplayDrivers matches; there is
    no yttrium12 (D3D12) entry;
  - drop TdrDebugMode and EnablePreemption, which are machine-wide.

Every edit is checked: if the template no longer has the line an edit
expects, the script fails rather than writing a different INF.

usage: make-inf.py TEMPLATE OUT.inf
"""
import re
import sys

DROP_FILES = re.compile(r"\b(libEGL|libGLESv1_CM|libGLESv2|opengl32)(_x64|_x86)?\.dll\b", re.I)
DROP_GLOBALS = re.compile(r'^\s*HKLM,"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers(\\Scheduler)?",\s*(TdrDebugMode|EnablePreemption),', re.I)

UMD = "viogpu_d3d10.dll"
WANT = {
    "UserModeDriverName": ",".join([r"%11%\viogpu_d3d10.dll"] * 3),
    "UserModeDriverNameWow": ",".join([r"%16425%\viogpu_d3d10.dll"] * 3),
    "InstalledDisplayDrivers": ",".join(["viogpu_d3d10"] * 3),
    "OpenGLDriverName": r"%11%\viogpu_wgl.dll",
    "OpenGLDriverNameWow": r"%16425%\viogpu_wgl.dll",
}
DWORDS = {"OpenGLFlags": "3", "OpenGLFlagsWow": "3", "OpenGLVersion": "4096", "OpenGLVersionWow": "4096"}
VULKAN = [
    r'HKLM,"SOFTWARE\Khronos\Vulkan\Drivers","%11%\virtio_icd.x86_64.json",%REG_DWORD%,0',
    r'HKLM,"SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers","%16425%\virtio_icd.x86.json",%REG_DWORD%,0',
]


def main():
    template, out = sys.argv[1:3]
    with open(template, encoding="ascii") as f:
        lines = f.read().splitlines()

    seen = set()
    result = []
    for line in lines:
        code = line.split(";", 1)[0]
        if DROP_FILES.search(code) or DROP_GLOBALS.match(code):
            seen.add("dropped:" + (DROP_FILES.search(code) or DROP_GLOBALS.match(code)).group(0))
            continue
        m = re.match(r"^(HKR,,\s*)(\w+)(\s*,\s*%REG_(MULTI_SZ|DWORD)%\s*,\s*)(.*)$", code.rstrip())
        if m and m.group(2) in WANT:
            line = m.group(1) + m.group(2) + m.group(3) + WANT[m.group(2)]
            seen.add(m.group(2))
        elif m and m.group(2) in DWORDS:
            if m.group(5).strip() != DWORDS[m.group(2)]:
                sys.exit(f"{m.group(2)} is {m.group(5)!r} in the template, expected {DWORDS[m.group(2)]}")
            seen.add(m.group(2))
        if re.search(r"yttrium12|lvp_icd|vulkan_lvp", code, re.I):
            sys.exit(f"unexpected entry left by stampinf: {line}")
        result.append(line)

    missing = [k for k in list(WANT) + list(DWORDS) if k not in seen]
    if missing:
        sys.exit(f"template lacks {missing}")
    text = "\n".join(result)
    for v in VULKAN:
        if v not in text:
            sys.exit(f"template lacks {v}")
    if not any(s.startswith("dropped:") for s in seen):
        sys.exit("template carried none of the files this script drops; check it by hand")

    with open(out, "w", newline="") as f:
        f.write("\r\n".join(result).rstrip("\r\n") + "\r\n")


if __name__ == "__main__":
    main()
