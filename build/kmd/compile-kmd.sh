#!/usr/bin/env bash
# Compile the Yttrium viogpu3d KMD (and VirtioLib) with clang-cl and lld-link
# against the WDK and SDK NuGet headers and libraries.
#
# Runs inside the build container (see build.sh). Inputs come from env:
#   SRC        patched source tree (VirtIO/, viogpu/, build/)
#   TOOLCHAIN  llvm-mingw root (clang, lld-link, llvm-lib, llvm-rc)
#   WDK SDK    unpacked NuGet packages (Microsoft.Windows.WDK.x64, SDK.CPP)
#   SDKX64     unpacked Microsoft.Windows.SDK.CPP.x64 (libs only, unused by the KMD)
#   OBJ        scratch directory for objects
#   OUT        output directory (receives viogpu3d.sys, viogpu3d.pdb)
#   DRIVER_VERSION  a.b.c.d, the PE version resource and INF DriverVer
#   SOURCE_DATE_EPOCH  fixed timestamp for __DATE__/__TIME__
set -euo pipefail

: "${SRC:?}" "${TOOLCHAIN:?}" "${WDK:?}" "${SDK:?}" "${OBJ:?}" "${OUT:?}" "${DRIVER_VERSION:?}"

KITVER=10.0.26100.0
WINC=$WDK/c/Include/$KITVER
SINC=$SDK/c/Include/$KITVER
KMLIB=$WDK/c/Lib/$KITVER/km/x64

CLANG=$TOOLCHAIN/bin/clang
LLD_LINK=$TOOLCHAIN/bin/lld-link
LLVM_LIB=$TOOLCHAIN/bin/llvm-lib
LLVM_RC=$TOOLCHAIN/bin/llvm-rc

# clang's own intrin.h must win over the MSVC one in km/crt, and must be
# included before wdm.h: it defines the port I/O intrinsics (__inbyte, ...)
# inline, where MSVC treats wdm.h's bare declarations as compiler builtins.
CLANG_RES=$("$CLANG" -print-resource-dir)

IFS=. read -r V1 V2 V3 V4 <<<"$DRIVER_VERSION"

mkdir -p "$OBJ"/virtiolib "$OBJ"/viogpu3d "$OBJ"/gen "$OUT"

# WPP is not available off Windows (tracewpp.exe). trace.h already routes
# DbgPrint to VirtioDebugPrintProc, so the only WPP users left are the
# per-file .tmh includes and WPP_INIT_TRACING/WPP_CLEANUP in driver.cpp.
# Generate stub .tmh files: the driver loses its ETW provider, nothing else.
for f in driver viogpu_queue viogpu_pci bitops viogpu_idr VirtIOPCILegacy VirtIOPCIModern; do
    printf '// generated: WPP disabled in the Linux build\n' >"$OBJ/gen/$f.tmh"
done
cat >>"$OBJ/gen/driver.tmh" <<'EOF'
#define WPP_INIT_TRACING(DriverObject, RegistryPath) do { UNREFERENCED_PARAMETER(DriverObject); UNREFERENCED_PARAMETER(RegistryPath); } while (0)
#define WPP_CLEANUP(DriverObject) do { UNREFERENCED_PARAMETER(DriverObject); } while (0)
EOF

# Windows sources include headers in whatever case; make every include root
# and the source tree case-insensitive for clang.
python3 "$(dirname "$0")/mkoverlay.py" "$OBJ/overlay.json" \
    "$WINC/km" "$WINC/shared" "$SINC/shared" "$SINC/um" "$SRC" "$OBJ/gen"

# Mirrors WindowsKernelModeDriver10.0 for "Win11 Release|x64": /kernel, /GS,
# stdcall default, wchar_t not built in, 8-byte packing, no RTTI or EH,
# NTDDI_VERSION for TargetVersion=Windows10 with the 26100 kit.
CLFLAGS=(
    --driver-mode=cl --target=x86_64-pc-windows-msvc -fms-compatibility-version=19.44
    /nologo /c /X /kernel /GS /Gy /Gz /Zp8 /GR- /Zc:wchar_t- /EHs-c- /O2 /Oi /Z7 /W3
    /Brepro -vfsoverlay "$OBJ/overlay.json"
    -Wno-nonportable-include-path -Wno-microsoft-include -Wno-pragma-pack
    -Wno-unknown-pragmas -Wno-ignored-pragma-intrinsic -Wno-microsoft-anon-tag
    -Wno-error=incompatible-function-pointer-types
    /I"$OBJ/gen"
    -imsvc "$CLANG_RES/include"
    -imsvc "$WINC/km/crt" -imsvc "$WINC/km" -imsvc "$SINC/shared" -imsvc "$WINC/shared" -imsvc "$SINC/um"
    /FI"$SINC/shared/warning.h" /FIintrin.h
    /D_WIN64 /D_AMD64_ /DAMD64 /DDEPRECATE_DDK_FUNCTIONS=1 /DMSC_NOOPT
    /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /DWINNT=1 /DNTDDI_VERSION=0x0A000010
    /DVENDOR_VER=RHEL.ver /DVER_OS=Win11 /DVER_ARCH=x64 /DCOPYRIGHT_CURRENT_YEAR=2021
    /D_NT_TARGET_MAJ=$V1 /D_RHEL_RELEASE_VERSION_=$V2 /D_BUILD_MAJOR_VERSION_=$V3
    /D_BUILD_MINOR_VERSION_=$V4 "/DRHEL_COPYRIGHT_STARTING_YEAR=20??"
)

cc() { # cc OBJDIR SOURCE [extra flags...]
    local objdir=$1 src=$2; shift 2
    local o="$objdir/$(basename "${src%.*}").obj"
    echo "  CC $src"
    "$CLANG" "${CLFLAGS[@]}" "$@" "$src" /Fo"$o"
}

echo "== VirtioLib"
for f in VirtIOPCICommon.c VirtIOPCILegacy.c VirtIOPCIModern.c VirtIORing-Packed.c VirtIORing.c; do
    cc "$OBJ/virtiolib" "$SRC/VirtIO/$f" /I"$SRC/VirtIO"
done
"$LLVM_LIB" /nologo /out:"$OBJ/virtiolib.lib" "$OBJ"/virtiolib/*.obj

echo "== viogpu3d"
GPU_INC=(/I"$SRC/viogpu/common" /I"$SRC/viogpu/shared" /I"$SRC/VirtIO" /I"$SRC/viogpu/viogpu3d"
         /DVIOGPU_3D=1 /std:c++17)
for f in common/edid.cpp common/viogpu_idr.cpp common/viogpu_pci.cpp common/viogpu_queue.cpp \
         viogpu3d/driver.cpp viogpu3d/viogpu_command.cpp viogpu3d/viogpu_device.cpp \
         viogpu3d/viogpu_allocation.cpp viogpu3d/viogpu_adapter.cpp \
         viogpu3d/viogpu_shmem_allocator.cpp common/baseobj.cpp common/bitops.cpp \
         viogpu3d/viogpu_vidpn.cpp; do
    cc "$OBJ/viogpu3d" "$SRC/viogpu/$f" "${GPU_INC[@]}"
done

echo "  RC viogpu3d.rc"
# The .rc is UTF-16 and includes headers in mixed case. Convert it to UTF-8,
# preprocess it with clang (which has the case-insensitive overlay), then
# compile the result with llvm-rc without its own preprocessor.
iconv -f UTF-16 -t UTF-8 "$SRC/viogpu/viogpu3d/viogpu3d.rc" | sed 's/\r$//' >"$OBJ/gen/viogpu3d.utf8.rc"
"$CLANG" --driver-mode=cl --target=x86_64-pc-windows-msvc -fms-compatibility-version=19.44 \
    /nologo /X /EP -vfsoverlay "$OBJ/overlay.json" -Wno-nonportable-include-path -Wno-microsoft-include \
    /I"$SRC/viogpu/viogpu3d" /I"$SRC/build" \
    -imsvc "$SINC/um" -imsvc "$SINC/shared" -imsvc "$WINC/km/crt" \
    /DRC_INVOKED /D_WIN64 /D_AMD64_ /DAMD64 /DNTDDI_VERSION=0x0A000010 /D_WIN32_WINNT=0x0A00 \
    /DVENDOR_VER=rhel.ver /DVER_OS=Win11 /DVER_ARCH=x64 \
    /D_NT_TARGET_MAJ=$V1 /D_RHEL_RELEASE_VERSION_=$V2 /D_BUILD_MAJOR_VERSION_=$V3 \
    /D_BUILD_MINOR_VERSION_=$V4 '/DRHEL_COPYRIGHT_YEARS=2026' \
    /Tc"$OBJ/gen/viogpu3d.utf8.rc" >"$OBJ/gen/viogpu3d.i.rc"
"$LLVM_RC" /nologo /no-preprocess /C 1252 /FO "$OBJ/viogpu3d.res" "$OBJ/gen/viogpu3d.i.rc"

echo "  LINK viogpu3d.sys"
# Mirrors the WDK driver link: /DRIVER, native subsystem 10.0, GsDriverEntry
# from bufferoverflowfastfailk.lib, no default libraries, INIT discardable.
"$LLD_LINK" /nologo /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:GsDriverEntry /NODEFAULTLIB \
    /MACHINE:X64 /RELEASE /INCREMENTAL:NO /OPT:REF /OPT:ICF /Brepro /TSAWARE:NO \
    /MERGE:_TEXT=.text /MERGE:_PAGE=PAGE /SECTION:INIT,d \
    /DEBUG /PDB:"$OUT/viogpu3d.pdb" /PDBALTPATH:viogpu3d.pdb \
    /OUT:"$OUT/viogpu3d.sys" \
    "$OBJ"/viogpu3d/*.obj "$OBJ/viogpu3d.res" "$OBJ/virtiolib.lib" \
    "$KMLIB/bufferoverflowfastfailk.lib" "$KMLIB/ntoskrnl.lib" "$KMLIB/hal.lib" \
    "$KMLIB/wmilib.lib" "$KMLIB/displib.lib"
