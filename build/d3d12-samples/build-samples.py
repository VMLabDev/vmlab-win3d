#!/usr/bin/env python3
"""Build DirectX-Graphics-Samples projects from their own .vcxproj files.

Reads each sample's Release|x64 configuration (sources, shader build steps,
preprocessor definitions, libraries, delay-loaded DLLs, deployed content) and
carries it out with clang-cl, lld-link and the Linux DXC. Nothing in the
sample is edited. The project's NuGet imports are replaced by their include
directories; the Agility SDK's D3D12Core.dll is deliberately not deployed.
"""
import argparse
import concurrent.futures
import os
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

SAMPLES = {
    "D3D12HelloTriangle": "Samples/Desktop/D3D12HelloWorld/src/HelloTriangle/D3D12HelloTriangle.vcxproj",
    "D3D12HelloTexture": "Samples/Desktop/D3D12HelloWorld/src/HelloTexture/D3D12HelloTexture.vcxproj",
    "D3D12Bundles": "Samples/Desktop/D3D12Bundles/src/D3D12Bundles.vcxproj",
    "D3D12nBodyGravity": "Samples/Desktop/D3D12nBodyGravity/src/D3D12nBodyGravity.vcxproj",
    "D3D12ExecuteIndirect": "Samples/Desktop/D3D12ExecuteIndirect/src/D3D12ExecuteIndirect.vcxproj",
}
CONFIG = "Release|x64"
NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"
# What %(AdditionalDependencies) expands to in a Visual Studio C++ project.
DEFAULT_LIBS = ["kernel32.lib", "user32.lib", "gdi32.lib", "winspool.lib", "comdlg32.lib",
                "advapi32.lib", "shell32.lib", "ole32.lib", "oleaut32.lib", "uuid.lib",
                "odbc32.lib", "odbccp32.lib"]


def applies(el):
    cond = el.get("Condition")
    return cond is None or CONFIG in cond


def tag(el):
    return el.tag[len(NS):]


def parse(path):
    root = ET.parse(path).getroot()
    p = {"cpp": [], "custom": [], "defs": [], "libs": [], "delay": [], "unicode": False,
         "subsystem": "Windows", "custom_default": None}
    for pg in root.iter(NS + "PropertyGroup"):
        if applies(pg):
            for e in pg:
                if tag(e) == "CharacterSet" and e.text == "Unicode":
                    p["unicode"] = True
    for idg in root.iter(NS + "ItemDefinitionGroup"):
        if not applies(idg):
            continue
        for group in idg:
            for e in group:
                text = (e.text or "").strip()
                name = tag(e)
                if tag(group) == "ClCompile" and name == "PreprocessorDefinitions":
                    p["defs"] += [d for d in text.split(";") if d and not d.startswith("%(")]
                elif tag(group) == "Link" and name == "AdditionalDependencies":
                    p["libs"] += [d for d in text.split(";") if d and not d.startswith("%(")]
                elif tag(group) == "Link" and name == "DelayLoadDLLs":
                    p["delay"] += [d for d in text.split(";") if d and not d.startswith("%(")]
                elif tag(group) == "Link" and name == "SubSystem":
                    p["subsystem"] = text
                elif tag(group) == "CustomBuild" and name == "Command":
                    p["custom_default"] = text
    for ig in root.iter(NS + "ItemGroup"):
        for it in ig:
            inc = it.get("Include")
            if tag(it) == "ClCompile":
                p["cpp"].append(inc)
            elif tag(it) == "CustomBuild":
                cmd = [e.text for e in it if tag(e) == "Command" and applies(e)]
                p["custom"].append((inc, cmd[0] if cmd else p["custom_default"]))
    return p


def run(cmd, cwd):
    print("+", " ".join(shlex.quote(c) for c in cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def custom_build(item, command, srcdir, outdir, dxc):
    stem = os.path.splitext(os.path.basename(item))[0]
    for line in command.splitlines():
        line = (line.replace("$(OutDir)", outdir + "/").replace("%(Filename)", stem)
                .replace("%(FullPath)", os.path.join(srcdir, item)).replace("%(Identity)", item))
        line = re.sub(r"\s*>\s*NUL\s*$", "", line)
        argv = shlex.split(line)
        if argv[0].lower() == "copy":
            shutil.copy(os.path.join(srcdir, argv[1]), argv[2])
        elif argv[0].lower() == "dxc.exe":
            run([dxc] + argv[1:], srcdir)
        else:
            sys.exit(f"unhandled custom build step: {line}")


def case_shim(srcdir, incdirs, shim):
    """Windows file names are case-insensitive and the samples rely on it
    (<D3Dcompiler.h> is d3dcompiler.h in the SDK). xwin's splat adds the
    links the SDK's own headers need; for each header a sample includes
    under another case, link the spelling it uses in a shim directory."""
    os.makedirs(shim, exist_ok=True)
    names = set()
    for f in os.listdir(srcdir):
        if f.endswith((".cpp", ".h")):
            with open(os.path.join(srcdir, f), encoding="utf-8-sig", errors="replace") as fh:
                names.update(re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', fh.read(), re.M))
    for n in sorted(names):
        if os.path.dirname(n) or any(os.path.exists(os.path.join(d, n)) for d in [srcdir] + incdirs):
            continue
        for d in incdirs:
            hit = [f for f in os.listdir(d) if f.lower() == n.lower()] if os.path.isdir(d) else []
            if hit:
                os.symlink(os.path.join(d, hit[0]), os.path.join(shim, n))
                print(f"case shim: {n} -> {os.path.join(d, hit[0])}", flush=True)
                break


def build(name, vcxproj, a):
    srcdir = os.path.dirname(vcxproj)
    p = parse(vcxproj)
    outdir = os.path.join(a.out, name)
    objdir = os.path.join(a.out, ".obj", name)
    os.makedirs(outdir, exist_ok=True)
    os.makedirs(objdir, exist_ok=True)

    for item, command in p["custom"]:
        custom_build(item, command, srcdir, outdir, a.dxc)

    agility = os.path.join(a.agility, "build", "native", "include")
    sdk_inc = os.path.join(a.sysroot, "Windows Kits", "10", "Include")
    sdk_inc = [os.path.join(sdk_inc, v, sub) for v in os.listdir(sdk_inc) for sub in ("um", "shared", "ucrt")]
    shim = os.path.join(objdir, "case-shim")
    case_shim(srcdir, [agility] + sdk_inc, shim)
    cl = ["clang", "--driver-mode=cl", "--target=x86_64-pc-windows-msvc", "/winsysroot", a.sysroot,
          "/nologo", "/c", "/O2", "/Oi", "/Gy", "/W3", "/EHsc", "/MD", "/GS",
          "-Wno-everything", "/Brepro",
          "/I", srcdir, "/I", agility, "/I", os.path.join(agility, "d3dx12"),
          "/I", os.path.dirname(agility),
          "/I", os.path.join(a.pix, "Include", "WinPixEventRuntime"), "/I", shim]
    cl += ["/D" + d for d in p["defs"]]
    if p["unicode"]:
        cl += ["/DUNICODE", "/D_UNICODE"]
    objs = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        futs = []
        for cpp in p["cpp"]:
            obj = os.path.join(objdir, os.path.splitext(cpp)[0] + ".obj")
            objs.append(obj)
            # "--" ends the options: clang-cl reads a leading "/" as one.
            futs.append(ex.submit(run, cl + ["/Fo" + obj, "--", os.path.join(srcdir, cpp)], srcdir))
        for f in futs:
            f.result()

    exe = os.path.join(outdir, name + ".exe")
    link = ["lld-link", "/nologo", "/winsysroot:" + a.sysroot, "/machine:x64",
            "/subsystem:" + p["subsystem"].lower(), "/opt:ref", "/opt:icf", "/dynamicbase",
            "/nxcompat", "/Brepro", "/out:" + exe] + objs + p["libs"] + DEFAULT_LIBS
    if p["delay"]:
        link += ["delayimp.lib"] + ["/delayload:" + d for d in p["delay"]]
    run(link, srcdir)
    # The D3D12SDKVersion/D3D12SDKPath exports make lld-link write an import
    # library; nothing uses it.
    for ext in (".lib", ".exp"):
        if os.path.exists(exe[:-4] + ext):
            os.remove(exe[:-4] + ext)


def main():
    ap = argparse.ArgumentParser()
    for opt in ("src", "sysroot", "dxc", "agility", "pix", "out"):
        ap.add_argument("--" + opt, required=True)
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    for name, rel in SAMPLES.items():
        print(f"== {name}", flush=True)
        build(name, os.path.join(a.src, rel), a)
    shutil.rmtree(os.path.join(a.out, ".obj"))


if __name__ == "__main__":
    main()
