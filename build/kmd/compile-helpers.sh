#!/usr/bin/env bash
# Compile the user-mode helpers the viogpu3d INF installs: viogpuap.exe and
# vgpusrv.exe (the viogpusc project). They use only Win32, cfgmgr32, D3DKMT
# and WTS APIs, so they build with llvm-mingw (mingw-w64 headers, ucrt) and
# link statically, like the MSVC /MT build upstream.
#
# Env: SRC, TOOLCHAIN, SDK, OBJ, OUT (see compile-kmd.sh).
set -euo pipefail

: "${SRC:?}" "${TOOLCHAIN:?}" "${SDK:?}" "${OBJ:?}" "${OUT:?}"

CXX=$TOOLCHAIN/bin/x86_64-w64-mingw32-clang++
STRIP=$TOOLCHAIN/bin/llvm-strip

mkdir -p "$OBJ/helpers" "$OUT"

KITVER=10.0.26100.0
SINC=$SDK/c/Include/$KITVER
TMP_INC=$OBJ/helpers/inc
mkdir -p "$TMP_INC"

# mingw-w64 has no d3dkmthk.h. Take it, and the d3dukmdt.h/d3dkmdt.h it
# needs, from the SDK, searched after the mingw headers so nothing else is
# shadowed.
for h in d3dkmthk.h d3dukmdt.h d3dkmdt.h; do
    cp "$SINC/shared/$h" "$TMP_INC/" 2>/dev/null || cp "$SINC/um/$h" "$TMP_INC/"
done

# The sources include a few headers with Windows casing; mingw-w64 ships
# them lower case.
python3 "$(dirname "$0")/mkoverlay.py" "$OBJ/helpers/overlay.json" "$SRC/viogpu" "$TMP_INC" \
    "$TOOLCHAIN/x86_64-w64-mingw32/include"

CXXFLAGS=(
    -O2 -std=c++17 -municode -DUNICODE -D_UNICODE -DNDEBUG -D_CONSOLE
    -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DNTDDI_VERSION=0x0A000010
    -fms-extensions -ivfsoverlay "$OBJ/helpers/overlay.json" -Wno-nonportable-include-path
    -idirafter "$TMP_INC" -include "$(dirname "$0")/mingw-compat.h"
    -ffile-prefix-map="$SRC"=. -Wl,--no-insert-timestamp
    -static -s
)

echo "  CXX viogpuap.exe"
"$CXX" "${CXXFLAGS[@]}" -I"$SRC/viogpu/viogpuap" -I"$SRC/viogpu/shared" \
    "$SRC"/viogpu/viogpuap/{GpuAdapter,GpuAdaptersMgr,pch,viogpuap}.cpp \
    "$SRC"/viogpu/shared/{log,pipe}.cpp \
    -o "$OUT/viogpuap.exe" -lcfgmgr32 -lgdi32 -luser32 -ladvapi32 -lshell32 -lole32 -luuid

echo "  CXX vgpusrv.exe"
"$CXX" "${CXXFLAGS[@]}" -I"$SRC/viogpu/viogpusc" -I"$SRC/viogpu/shared" \
    "$SRC"/viogpu/viogpusc/{pch,Service,Session,SessionMgr,utils,viogpusc}.cpp \
    "$SRC"/viogpu/shared/{log,pipe}.cpp \
    -o "$OUT/vgpusrv.exe" -lwtsapi32 -luserenv -ladvapi32 -luser32 -lgdi32 -lshell32 -lole32
