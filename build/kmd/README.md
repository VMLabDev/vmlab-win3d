# kmd

Builds the Yttrium viogpu3d kernel-mode display driver (KMD), its two
user-mode helpers, and the INF files, for x64 Windows 10 and 11. The source is
`arehnman/yttrium-virtio-gpu` at tag `yttrium-experimental-2026-09-27`
(`b99d951a1c97f6fb3a1adbc08a4d2ab9ce9f5a15`), a fork of virtio-win's
kvm-guest-drivers-windows, plus the patch in `patches/kmd/`.

Upstream builds with Visual Studio and the WDK on Windows. This build runs on
Linux: clang-cl and lld-link from llvm-mingw compile and link against the
headers and kernel libraries in Microsoft's WDK and SDK NuGet packages. No
Windows tool runs at any step.

The build does not sign anything. `build/signing/sign-driver.sh` signs the
output, and `build/package/build.sh` assembles the package
([decision 0001](../../docs/decisions/0001-signing.md)).

## Run

```sh
build/kmd/build.sh
```

The script downloads the pinned inputs on the host, checks each against its
sha256 in `PINS`, and caches them in `$WIN3D_CACHE` (default `.scratch/cache`
in the repository). It then builds the image `vmlab-win3d/kmd-build:1` from
`Dockerfile` and runs `inner.sh` in it as your uid, with `--network none`. The
sources, kits and toolchain are mounted read-only. Each run deletes
`out/kmd/` first.

| Variable | Default | Effect |
| --- | --- | --- |
| `WIN3D_CACHE` | `.scratch/cache` | Download and unpack cache |
| `KMD_DRIVER_VERSION` | `100.6.101.58100` | The PE version resource and the INF's `DriverVer` version. Upstream's default is `100.6.101.58000`; the last field marks this repository's build, so it ranks above the upstream release and the prior art's r10 build (`58001`). |

## Outputs

```
out/kmd/x64/viogpu3d.sys                  the KMD
out/kmd/x64/viogpu3d.pdb                  its symbols (not packaged)
out/kmd/x64/vgpusrv.exe                   the "VioGpu Resolution Service"
out/kmd/x64/viogpuap.exe                  started by vgpusrv in each user session
out/kmd/x64/viogpu3d.inf                  kernel-only INF
out/kmd/viogpu3d_x64_wow64.inf.template   full INF, the input to build/package/make-inf.py
out/kmd/BUILDINFO                         driver version, pins, patch hashes, output sha256
```

## How it builds

`inner.sh` runs these steps in the container:

1. Unpacks llvm-mingw and the two NuGet packages (they are zip files).
2. Copies `VirtIO/`, `viogpu/` and `build/` from the source tree to the fixed
   path `/tmp/kmd-work/src`, and applies `patches/kmd/*.patch` in name order.
3. Sets `SOURCE_DATE_EPOCH` and the INF date to upstream's tag date,
   2026-09-27.
4. Runs `compile-kmd.sh`, `compile-helpers.sh` and `stampinf.py`.

### The KMD (`compile-kmd.sh`)

The compiler flags mirror the WDK's `WindowsKernelModeDriver10.0` toolset
for "Win11 Release|x64": `/kernel /GS /Gz /Zp8 /GR- /Zc:wchar_t- /EHs-c- /O2`,
`NTDDI_VERSION=0x0A000010`, the 10.0.26100.0 kit. `/Brepro` keeps the
output deterministic.

- **Case.** Windows sources and kit headers `#include` names in any case.
  `mkoverlay.py` writes a clang VFS overlay that makes the kit include
  directories and the source tree case-insensitive.
- **Intrinsics.** clang's own `intrin.h` is force-included before `wdm.h`, so
  the port I/O intrinsics are defined inline rather than as bare MSVC
  builtins.
- **WPP.** `tracewpp.exe` exists only on Windows. The script writes empty
  `.tmh` stubs and defines `WPP_INIT_TRACING` and `WPP_CLEANUP` as no-ops, so
  the driver has no ETW provider.
- **Sources.** VirtioLib (`VirtIO/VirtIOPCICommon.c`, `VirtIOPCILegacy.c`,
  `VirtIOPCIModern.c`, `VirtIORing.c`, `VirtIORing-Packed.c`) is archived with
  `llvm-lib`. The driver is 13 `.cpp` files from `viogpu/common/` and
  `viogpu/viogpu3d/`, listed in the script.
- **Resources.** `viogpu3d.rc` is UTF-16 and includes mixed-case headers. The
  script converts it to UTF-8, preprocesses it with clang (which has the
  overlay), and compiles the result with `llvm-rc /no-preprocess`.
- **Link.** `lld-link /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:GsDriverEntry
  /NODEFAULTLIB` against `bufferoverflowfastfailk.lib`, `ntoskrnl.lib`,
  `hal.lib`, `wmilib.lib` and `displib.lib` from the WDK.

### The helpers (`compile-helpers.sh`)

`vgpusrv.exe` (upstream's `viogpusc` project) and `viogpuap.exe` use only
Win32, cfgmgr32, D3DKMT and WTS APIs. They build with llvm-mingw's
`x86_64-w64-mingw32-clang++`, linked statically like upstream's `/MT` build.
mingw-w64 has no `d3dkmthk.h`, so the script takes it, `d3dukmdt.h` and
`d3dkmdt.h` from the SDK package and searches them after the mingw headers.
`mingw-compat.h` is force-included to supply `__FUNCTIONW__` and two legacy
SAL macros.

### The INF (`stampinf.py`)

The source has an INF template, `viogpu/viogpu3d/viogpu3d_x64_wow64.inx`.
Upstream's MSBuild turns it into an INF with three Windows-only steps.
`stampinf.py` does the same:

| Placeholder or entry | Becomes | Upstream step |
| --- | --- | --- |
| `$ARCH$` | `amd64.10.0`, so the decoration is `NTamd64.10.0...16299` | `stampinf.exe -a`, with the `InfArch` from `viogpu3d.vcxproj` |
| `DriverVer` | `09/27/2026,<KMD_DRIVER_VERSION>` | `stampinf.exe` |
| `INX_COPYRIGHT_1`, `INX_COPYRIGHT_2`, `INX_COMPANY`, `INX_PREFIX_VENDOR`, `INX_PREFIX_VIRTIO`, `INX_PREFIX_QEMU`, `INX_PLATFORM_DRIVERS_DIR`, `_INX_SUBSYS_VENDOR_ID` | The Hardsoft vendor strings, `13` (driver store), `1AF4` | `Driver.Common.targets` |
| Lavapipe files (`vulkan_lvp_*.dll`, `lvp_icd.*.json`) and the `yttrium12` D3D12 slot | Removed, because those payloads are not built | `tools/prepare_package_inx.ps1` |

It writes two files:

- `viogpu3d_x64_wow64.inf.template`: the full INF. It still lists every Mesa
  file upstream ships, including `libEGL`, `libGLESv*` and `opengl32`.
  `build/package/make-inf.py` turns it into the package INF: it drops those
  files and the machine-wide `TdrDebugMode` and `EnablePreemption` values,
  and sets `UserModeDriverName[Wow]` to `viogpu_d3d10.dll` three times. It
  fails if the template no longer has a line it expects to change.
- `x64/viogpu3d.inf`: the kernel-only INF. It installs `viogpu3d.sys`,
  `vgpusrv.exe` and `viogpuap.exe` with no UMD, OpenGL or Vulkan entries.
  The signing load test uses it.

The INF matches `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01`. It installs
`viogpu3d.sys`, `vgpusrv.exe` and `viogpuap.exe` to DIRID 13 (the driver
store), adds the kernel service `VioGpu3D` and the auto-start service
`vgpusrv`, and writes `MSISupported` = 0 and `MessageNumberLimit` = 4 under
the device's interrupt-management key.

## Pins

`PINS` lists each input with its sha256 (git sources by commit). The image's
Debian packages are pinned by version in `Dockerfile`: `python3`, `xz-utils`,
`patch` and `unzip`.

| Input | Version |
| --- | --- |
| yttrium-virtio-gpu | `b99d951a`, tag `yttrium-experimental-2026-09-27` |
| llvm-mingw | 20261006 (LLVM 23.1.3, ucrt) |
| Microsoft.Windows.WDK.x64 | 10.0.26100.6584 |
| Microsoft.Windows.SDK.CPP | 10.0.26100.6584 (headers only) |
| debian | 12, by digest |

`docs/stack.md` has the full table, generated from `PINS`.

## Patches

| Patch | What it does |
| --- | --- |
| `0001-kmd-r10-copy.patch` | Converts R10G10B10A2 sources to 8-bit RGBA or BGRA in the KMD's CPU present blit, so 10-bit back buffers present correctly. It is a byte-identical copy of the prior art's `R/kmd-r10-copy.patch` on the same base (prior-art §2.2, `NW` F93). |

The patch has no mail header. `PINS` records its sha256 and origin.

## Reproducibility

The build is bit-reproducible on this host. A run on 2026-10-17 from the
`docs` worktree gave the same sha256 for all six outputs, including the PDB,
as the run recorded in `docs/evidence/kmd/00-signing-host.txt` from the `kmd`
worktree:

```
7fefcfcac00f1e9fb690036adbdba941517115b597f4cde657a336075a8cc07e  x64/viogpu3d.sys
5d187833571a6bdb13b0ac0b6b72ca6d48ab5620dae599d8af04131802126116  x64/viogpu3d.pdb
fd091e6d5d87ffca9c5e2b6c3c5e0d3d358a066304a4a464faeddc6e39f8bb48  x64/vgpusrv.exe
fd78ce1c3a1cb30e7995310df3440953ea9a2bab6ce8e23b02a65c33e0761f50  x64/viogpuap.exe
50b20f4493076fdc2b6f2d8920b57e78eb9087ac6bd2d399115278f7513b36f2  x64/viogpu3d.inf
601ddd307f995980f4e1729cb4399b8de476863087fdf9f3ea894a4ebaf6bdf7  viogpu3d_x64_wow64.inf.template
```

The work tree is always `/tmp/kmd-work` in the container, `/Brepro` removes
timestamps from objects and the PE, and the helpers use
`-ffile-prefix-map` and `--no-insert-timestamp`.

## Caveats

- **Release, not Debug.** The build defines no `DBG`. The driver's
  `DbgPrint` calls compile to nothing, and with WPP stubbed there is no
  trace output at all. The prior art ran upstream's Debug build, which prints
  to the kernel debugger and breaks into it when one is attached. There is
  no Debug variant of this script yet.
- **lld-link `/SECTION` replaces attributes.** `INIT,d` alone left
  `DriverEntry` in a non-executable section, and the guest bugchecked 0x1A on
  load. The link uses `/SECTION:INIT,ERD`. See
  `docs/evidence/kmd/README.md`, "Earlier runs".
- **INF decoration.** `NTamd64...16299`, without `.10.0`, puts 16299 in the
  ProductType field, and the INF then matches no device. `stampinf.py`
  writes `amd64.10.0`, as upstream does.
- **Code 43 with the kernel-only INF.** Installed alone, the KMD loads and
  starts, then reports Code 43, because there is no UMD. The full package
  reports problem code 0 (`docs/evidence/package/README.md`).
- **Needs a 3D device.** The KMD requires `VIRTIO_GPU_F_VIRGL`, so it fails
  to start on a plain `virtio-vga`. Use vmlab's `gpu { mode = "vulkan" }`.
- **No static analysis.** Upstream's tree has Static Driver Verifier scripts
  (`RunSdv.bat`), which need Windows. This build runs none. clang runs with
  `-Wno-error=incompatible-function-pointer-types` and several
  MSVC-compatibility warnings off.
- **x64 only.** The script builds no other architecture.
