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
The loader is built with `USE_GAS=ON`, so clang assembles its trampolines for
unknown device functions. vulkaninfo is built with cube, the mock ICD and the
tests turned off, so it needs no other dependencies. The build never runs
`update_deps.py`.
