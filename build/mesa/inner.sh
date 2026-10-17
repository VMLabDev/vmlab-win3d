#!/usr/bin/env bash
# Runs inside the toolchain image (see build.sh). The checkout is at /w.
set -euo pipefail
pins=/w/build/mesa/PINS
patches=/w/patches/mesa
out=/w/out/mesa
work=$out/.work
dl=$work/downloads
jobs="${MESA_JOBS:-$(nproc)}"

SOURCE_DATE_EPOCH="$(awk '$1 == "epoch" { print $2 }' "$pins")"
export SOURCE_DATE_EPOCH TZ=UTC LC_ALL=C.UTF-8

# Fetch (or reuse) each pinned download and check it against PINS.
mkdir -p "$dl"
declare -A file
while read -r kind name version sha url; do
  [[ "$kind" == src ]] || continue
  f="$dl/$name-${url##*/}"
  if [[ ! -f "$f" ]] || ! echo "$sha  $f" | sha256sum --check --status; then
    echo "fetch $name $version"
    curl --fail --silent --show-error --location --retry 3 -o "$f.part" "$url"
    mv "$f.part" "$f"
  fi
  if ! echo "$sha  $f" | sha256sum --check --status; then
    echo "error: sha256 mismatch for $f" >&2
    echo "  want $sha" >&2
    echo "  got  $(sha256sum "$f" | cut -d' ' -f1)" >&2
    exit 1
  fi
  file[$name]=$f
done < "$pins"

# Fresh source tree: pinned tarball, then the local patch series in order.
src=$work/src
rm -rf "$src"
mkdir -p "$src"
tar -C "$src" --strip-components=1 -xzf "${file[mesa]}"
mkdir -p "$src/subprojects/DirectX-Headers-1.0"
tar -C "$src/subprojects/DirectX-Headers-1.0" --strip-components=1 \
  -xzf "${file[DirectX-Headers]}"
for p in "$patches"/*.patch; do
  echo "apply $(basename "$p")"
  patch --directory="$src" --strip=1 --forward --batch --quiet < "$p"
done
python3 /w/build/mesa/prepare-winddk.py "$src" \
  "${file[windows-sdk-cpp]}" "${file[windows-wdk-x64]}"

prefix_map="-ffile-prefix-map=$work=/mesa"
# -static: link libc++, libunwind and winpthread into the DLLs.
# --no-insert-timestamp: zero the PE header timestamp.
ldflags="-static -Wl,--no-insert-timestamp"

build_arch() {
  local triple=$1 dir=$2 icdarch=$3 bits=$4
  local b=$work/build-$dir p=$work/prefix-$dir
  rm -rf "$b" "$p" "$out/$dir"
  meson setup "$b" "$src" --cross-file "/opt/cross/meson-$triple.ini" \
    --buildtype=release --prefix="$p" --wrap-mode=nodownload \
    -Dc_args="$prefix_map" -Dcpp_args="$prefix_map" \
    -Dc_link_args="$ldflags" -Dcpp_link_args="$ldflags" \
    -Dplatforms=windows -Dgallium-drivers=zink \
    -Dgallium-yttrium=true -Dgallium-d3d10umd=true \
    -Dgallium-wgl-dll-name=viogpu_wgl -Dgallium-d3d10-dll-name=viogpu_d3d10 \
    -Dvulkan-drivers=virtio -Dllvm=disabled -Dzlib=disabled -Dzstd=disabled \
    -Dexpat=disabled -Dbuild-tests=false
  ninja -C "$b" -j "$jobs" \
    src/gallium/targets/d3d10umd/viogpu_d3d10.dll \
    src/gallium/targets/wgl/viogpu_wgl.dll \
    src/virtio/vulkan/libvulkan_virtio.dll \
    src/virtio/vulkan/virtio_icd.$icdarch.json

  mkdir -p "$out/$dir"
  local dll
  for dll in src/gallium/targets/d3d10umd/viogpu_d3d10.dll \
             src/gallium/targets/wgl/viogpu_wgl.dll \
             src/virtio/vulkan/libvulkan_virtio.dll; do
    llvm-strip --strip-unneeded -o "$out/$dir/$(basename "$dll")" "$b/$dll"
  done
  # Meson writes an ICD manifest for a Unix-style install tree. Write one
  # that sits next to the DLL instead; the loader resolves the relative path
  # against the manifest's directory.
  local api
  api="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["ICD"]["api_version"])' \
    "$b/src/virtio/vulkan/virtio_icd.$icdarch.json")"
  printf '{\n    "file_format_version": "1.0.1",\n    "ICD": {\n        "library_path": ".\\\\libvulkan_virtio.dll",\n        "library_arch": "%s",\n        "api_version": "%s"\n    }\n}\n' \
    "$bits" "$api" > "$out/$dir/virtio_icd.json"
}

build_arch x86_64 x64 x86_64 64
build_arch i686 x86 i686 32

cd "$out"
sha256sum x64/* x86/*
