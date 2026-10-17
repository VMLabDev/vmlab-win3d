# vulkan-sdk

Builds the Khronos Vulkan loader (`vulkan-1.dll`, from Vulkan-Loader) and
`vulkaninfo.exe` (from Vulkan-Tools) for x86_64 and i686 Windows. The build
uses the shared llvm-mingw toolchain image from `build/toolchain/`.

## Run

```sh
build/vulkan-sdk/build.sh
```

The script builds or reuses the toolchain image and then runs `inner.sh` in a
container as your uid. Network access is used only to fetch the pinned source
tarballs. Each run checks the sha256 of every tarball, also when the tarball is
already cached. Each run deletes and recreates the build trees, so you can run
the script again safely.

## Outputs

```
out/vulkan-sdk/x64/vulkan-1.dll   out/vulkan-sdk/x64/vulkaninfo.exe
out/vulkan-sdk/x86/vulkan-1.dll   out/vulkan-sdk/x86/vulkaninfo.exe
```

The build links libc++, libunwind and winpthread statically (`-static`). The
outputs import only Windows system DLLs: KERNEL32, ADVAPI32, CFGMGR32, USER32,
GDI32 and the UCRT `api-ms-win-crt-*` API sets. `vulkaninfo.exe` loads
`vulkan-1.dll` at run time. The intermediate files and the download cache go
in `out/vulkan-sdk/.work/`.

The build is set up to be reproducible. It uses a Release build at `-O2`,
`-ffile-prefix-map`, `-Wl,--no-insert-timestamp`, stripped binaries, and a
`SOURCE_DATE_EPOCH` taken from PINS.

## Pins

`PINS` records each input: its tag, the commit the tag pointed to, the tarball
sha256 and the URL. All three repositories use the tag `vulkan-sdk-1.4.363.0`.
To change the version, update all three `src` lines and the `epoch` line.
The loader is built with `BUILD_DLL_VERSIONINFO`, and vulkaninfo with
`VULKANINFO_BUILD_DLL_VERSIONINFO`, set to the version in the tag
(`vulkan-sdk-1.4.363.0` gives `1.4.363.0`), as the LunarG Vulkan Runtime
build does. Without them both version resources say "Dev Build". The loader
is built with `USE_GAS=ON`, so clang assembles its trampolines for unknown
device functions. vulkaninfo is built with cube, the mock ICD and the
tests turned off, so it needs no other dependencies. The build never runs
`update_deps.py`.

## Version resource

Both binaries carry the SDK version, so the installer's version check, its
journal and the Windows file properties show it. Read from the x64 and x86
builds with `strings -el` and `llvm-readobj --coff-resources`:

| Field | `vulkan-1.dll` | `vulkaninfo.exe` |
| --- | --- | --- |
| `FILEVERSION`, `PRODUCTVERSION` (binary) | 1.4.363.0 (`00040001 016B0000`) | 1.4.363.0 |
| `FileDescription` | Vulkan Loader | vulkaninfo |
| `FileVersion`, `ProductVersion` | 1.4.363.0 | 1.4.363.0 |
| `ProductName` | Vulkan Runtime | Vulkaninfo |
| `LegalCopyright` | Copyright (C) 2015-2026 | Copyright (C) 2015-2026 |

Before this, both said "Dev Build" (`Vulkan Loader - Dev Build`,
`1.4.363.Dev Build`). The numeric version was already 1.4.363.0. The
copyright year comes from CMake's `string(TIMESTAMP)`, which follows
`SOURCE_DATE_EPOCH`, so it does not depend on the build date.

## Reproducibility

Bit-reproducible on this host. A first run, a second run on its leftovers, and
a run from a fresh clone with an empty `out/` all gave the same sha256.
No other machine was tried. Toolchain image
`vmlab-win3d/toolchain:ab524e07f89c`, 2026-10-17:

```
0871d16d06f2c7c2f60c9b68e5ed3a61a069bf525d8f7cad2d3c88f3ce71e219  vulkan-sdk/x64/vulkan-1.dll
f3c7ee6c42538394f9e5b8c5cb8568982b300fd20f86d8e1f09d89a9b894e923  vulkan-sdk/x64/vulkaninfo.exe
36edc9f67cbec1c5fb8c510ab08efea18afffbd40d77b829d39f1e76236416cd  vulkan-sdk/x86/vulkan-1.dll
446fdd03f4d23c686f2e89fb3551f874ac4fce9620f885abf7a37cc692e37ad7  vulkan-sdk/x86/vulkaninfo.exe
```
