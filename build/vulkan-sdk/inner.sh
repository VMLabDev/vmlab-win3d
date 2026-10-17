#!/usr/bin/env bash
# Runs inside the toolchain image (see build.sh). The checkout is at /w.
set -euo pipefail
pins=/w/build/vulkan-sdk/PINS
out=/w/out/vulkan-sdk
work=$out/.work
dl=$work/downloads

SOURCE_DATE_EPOCH="$(awk '$1 == "epoch" { print $2 }' "$pins")"
export SOURCE_DATE_EPOCH TZ=UTC LC_ALL=C.UTF-8

# Fetch (or reuse) each pinned tarball and check it against PINS.
mkdir -p "$dl"
declare -A srcdir
while read -r kind name tag commit sha url; do
  [[ "$kind" == src ]] || continue
  file="$dl/$name-$tag.tar.gz"
  if [[ ! -f "$file" ]] || ! echo "$sha  $file" | sha256sum --check --status; then
    echo "fetch $name $tag ($commit)"
    curl --fail --silent --show-error --location --retry 3 -o "$file.part" "$url"
    mv "$file.part" "$file"
  fi
  if ! echo "$sha  $file" | sha256sum --check --status; then
    echo "error: sha256 mismatch for $file" >&2
    echo "  want $sha" >&2
    echo "  got  $(sha256sum "$file" | cut -d' ' -f1)" >&2
    exit 1
  fi
  srcdir[$name]="$work/src/$name"
  rm -rf "${srcdir[$name]}"
  mkdir -p "${srcdir[$name]}"
  tar -C "${srcdir[$name]}" --strip-components=1 -xzf "$file"
done < "$pins"

# Vulkan-Headers: header-only, installed once and shared by both arches.
# Its project() still probes a compiler, so give it the x86_64 cross one.
hdr=$work/headers
rm -rf "$work/build-headers" "$hdr"
cmake -S "${srcdir[Vulkan-Headers]}" -B "$work/build-headers" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/opt/cross/cmake-x86_64.cmake \
  -DCMAKE_INSTALL_PREFIX="$hdr" -DVULKAN_HEADERS_ENABLE_MODULE=OFF \
  -DVULKAN_HEADERS_ENABLE_TESTS=OFF
cmake --install "$work/build-headers"

prefix_map="-ffile-prefix-map=$work=/vulkan-sdk"
cflags="-O2 $prefix_map"
# -static: link libc++, libunwind and winpthread into the binaries.
# -s: strip symbols. --no-insert-timestamp: zero the PE header timestamp.
ldflags="-static -s -Wl,--no-insert-timestamp"

build_arch() {
  local triple=$1 dir=$2
  local tc=/opt/cross/cmake-$triple.cmake
  local bl=$work/build-$dir-loader bt=$work/build-$dir-tools
  rm -rf "$bl" "$bt" "$out/$dir"
  local common=(
    -G Ninja
    -DCMAKE_TOOLCHAIN_FILE="$tc"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_FLAGS="$cflags"
    -DCMAKE_CXX_FLAGS="$cflags"
    -DCMAKE_ASM_FLAGS="$prefix_map"
    -DCMAKE_EXE_LINKER_FLAGS="$ldflags"
    -DCMAKE_SHARED_LINKER_FLAGS="$ldflags"
    -DCMAKE_C_FLAGS_RELEASE=-DNDEBUG
    -DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG
    -DVulkanHeaders_DIR="$hdr/share/cmake/VulkanHeaders"
  )

  # USE_GAS: assemble the unknown-function trampolines with clang (the MASM
  # path wants jwasm/uasm, which the image does not have).
  cmake -S "${srcdir[Vulkan-Loader]}" -B "$bl" "${common[@]}" \
    -DUSE_GAS=ON -DBUILD_TESTS=OFF -DLOADER_CODEGEN=OFF -DUPDATE_DEPS=OFF
  cmake --build "$bl" --target vulkan

  cmake -S "${srcdir[Vulkan-Tools]}" -B "$bt" "${common[@]}" \
    -DBUILD_CUBE=OFF -DBUILD_ICD=OFF -DBUILD_TESTS=OFF -DBUILD_VULKANINFO=ON \
    -DUPDATE_DEPS=OFF
  cmake --build "$bt" --target vulkaninfo

  mkdir -p "$out/$dir"
  cp "$bl/loader/vulkan-1.dll" "$out/$dir/"
  cp "$bt/vulkaninfo/vulkaninfo.exe" "$out/$dir/"
}

build_arch x86_64 x64
build_arch i686 x86

cd "$out"
sha256sum x64/* x86/*
