#!/usr/bin/env python3
"""Turn viogpu3d_x64_wow64.inx into INF files, as the upstream MSBuild does.

Upstream runs three steps on Windows:
  1. tools/prepare_package_inx.ps1 drops the Lavapipe and yttrium12 entries
     when those payloads are absent (they are, here);
  2. stampinf.exe replaces $ARCH$ and writes DriverVer;
  3. Driver.Common.targets substitutes the INX_* vendor strings.

This script does the same with no Windows tools, and writes two INFs:
  --full         every file the upstream package lists (KMD, helpers, Mesa
                 UMDs, Vulkan ICD). A template for the package slice, which
                 supplies the Mesa files.
  --kernel-only  only viogpu3d.sys, vgpusrv.exe and viogpuap.exe, with no
                 UMD registration. Installable on its own; used for the
                 signing load test.

usage: stampinf.py INX --version A.B.C.D --date MM/DD/YYYY
                   --full OUT.inf --kernel-only OUT.inf
"""
import argparse
import re

SUBSTITUTIONS = {
    "INX_COPYRIGHT_1": "Copyright (c) 2026 Hardsoft Swed.",
    "INX_COPYRIGHT_2": "",
    "INX_COMPANY": "Hardsoft Swed",
    "INX_PREFIX_VENDOR": "Hardsoft Swed ",
    "INX_PREFIX_VIRTIO": "",
    "INX_PREFIX_QEMU": "",
    "INX_PLATFORM_DRIVERS_DIR": "13",
    "_INX_SUBSYS_VENDOR_ID": "1AF4",
}

# prepare_package_inx.ps1 with no Lavapipe and no yttrium12 payload.
ABSENT_PAYLOAD = ["vulkan_lvp_x64.dll", "lvp_icd.x86_64.json",
                  "vulkan_lvp_x86.dll", "lvp_icd.x86.json", "yttrium12_x64.dll"]

# Everything the Mesa build provides. The kernel-only INF drops these files,
# their CopyFiles sections and the registry values that point at them.
MESA_FILE = re.compile(
    r"(viogpu_d3d10|viogpu_wgl|libEGL|libGLESv1_CM|libGLESv2|libvulkan_virtio|opengl32)"
    r"(_x64|_x86)?\.dll|virtio_icd\.x86(_64)?\.json", re.I)
MESA_SECTIONS = ["VioGpu3D_Files.Usermode", "VioGpu3D_Files.Wow64",
                 "VioGpu3D_Files.Vulkan", "VioGpu3D_Files.VulkanWow64"]
UMD_VALUES = re.compile(
    r"^HKR,,\s*(UserModeDriverName|OpenGLDriverName|OpenGLFlags|OpenGLVersion|InstalledDisplayDrivers)",
    re.I)


def common(lines, version, date):
    out = []
    for line in lines:
        # prepare_package_inx.ps1: drop the DX12 slot, keep DX9/10/11.
        line = re.sub(r",%11%\\yttrium12\.dll\s*$", "", line)
        line = re.sub(r",yttrium12\s*$", "", line)
        if any(tok in line for tok in ABSENT_PAYLOAD):
            continue
        # stampinf -a $(InfArch). viogpu3d.vcxproj sets InfArch to
        # amd64.10.0 for Win11 because the INX already carries "...16299";
        # "NTamd64...16299" alone would put 16299 in the ProductType field and
        # match no system.
        line = line.replace("$ARCH$", "amd64.10.0")
        if re.match(r"^\s*DriverVer\s*=", line, re.I):
            line = f"DriverVer = {date},{version}"
        for k, v in SUBSTITUTIONS.items():
            line = line.replace(k, v)
        out.append(line)
    return out


def kernel_only(lines):
    out = []
    section = None
    for line in lines:
        m = re.match(r"^\s*\[([^\]]+)\]", line)
        if m:
            section = m.group(1)
            if section in MESA_SECTIONS:
                continue
        if section in MESA_SECTIONS:
            continue
        if MESA_FILE.search(line) and not line.lstrip().startswith(";"):
            continue
        if UMD_VALUES.match(line):
            continue
        if re.match(r"^\s*HKLM,\"SOFTWARE\\(WOW6432Node\\)?Khronos", line, re.I):
            continue
        if re.match(r"^\s*CopyFiles\s*=", line, re.I):
            parts = [p.strip() for p in line.split("=", 1)[1].split(",")]
            line = "CopyFiles=" + ", ".join(p for p in parts if p not in MESA_SECTIONS)
        if re.match(r"^\s*VioGpu3D_Files\.(Usermode|Wow64|Vulkan|VulkanWow64)\s*=", line):
            continue
        out.append(line)
    return out


def write(path, lines):
    # INF files are read as ANSI or UTF-16; plain ASCII with CRLF is safe.
    text = "\r\n".join(lines).rstrip("\r\n") + "\r\n"
    text.encode("ascii")
    with open(path, "w", newline="") as f:
        f.write(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inx")
    ap.add_argument("--version", required=True)
    ap.add_argument("--date", required=True)
    ap.add_argument("--full", required=True)
    ap.add_argument("--kernel-only", required=True)
    a = ap.parse_args()
    with open(a.inx, encoding="utf-8-sig") as f:
        lines = f.read().splitlines()
    stamped = common(lines, a.version, a.date)
    write(a.full, stamped)
    write(a.kernel_only, kernel_only(stamped))


if __name__ == "__main__":
    main()
