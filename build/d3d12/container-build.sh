#!/usr/bin/env bash
# Runs inside the shared toolchain image (build/toolchain); started by build.sh.
# /repo is this repository (read-only), /out is out/d3d12 (read-write).
#
# Sources and build trees live in /out/.work. A stamp over the pins, patches
# and build scripts decides whether they are still valid: when it matches,
# the build is incremental; when it does not, sources are reset to their pins,
# patches are re-applied and the build trees are recreated.
set -euo pipefail

repo=/repo
out=/out
work="$out/.work"
jobs="${JOBS:-$(nproc)}"
mkdir -p "$work"

pin() { awk -v n="$1" '$1 == n { print $2, $4 }' "$repo/build/d3d12/PINS"; }
read -r vkd3d_url vkd3d_commit < <(pin vkd3d-proton)
read -r dxil_url dxil_commit < <(pin dxil-spirv)
read -r dxvk_url dxvk_commit < <(pin dxvk)

stamp="$( (echo "${TOOLCHAIN_IMAGE:-}"; cd "$repo" && find build/d3d12/PINS build/d3d12/container-build.sh build/d3d12/cross patches/vkd3d-proton patches/dxil-spirv patches/dxvk -type f \
  | LC_ALL=C sort | xargs sha256sum) | sha256sum | cut -c1-16)"

# fetch <dir> <url> <commit>: check out <commit> in <dir>, discarding changes.
fetch() {
  local dir="$1" url="$2" commit="$3"
  if [[ ! -d "$dir/.git" && ! -f "$dir/.git" ]]; then
    git init -q "$dir"
    git -C "$dir" remote add origin "$url"
  fi
  if ! git -C "$dir" cat-file -e "$commit^{commit}" 2>/dev/null; then
    git -C "$dir" fetch -q --depth 1 origin "$commit"
  fi
  git -C "$dir" checkout -q -f --detach "$commit"
  git -C "$dir" clean -qffdx
}

# submodules <dir>: check out the superproject's pinned submodules.
submodules() {
  git -C "$1" submodule sync -q --recursive
  git -C "$1" submodule update -q --init --recursive --force --depth 1
  git -C "$1" submodule foreach -q --recursive 'git clean -qffdx'
}

# apply <dir> <patch dir>: apply every patch in order.
apply() {
  local p
  shopt -s nullglob
  for p in "$2"/*.patch; do
    echo "applying $(basename "$p")"
    git -C "$1" apply --whitespace=nowarn "$p"
  done
  shopt -u nullglob
}

prepare_sources() {
  echo "== preparing sources (stamp $stamp)"
  rm -rf "$work/build"

  fetch "$work/vkd3d-proton" "$vkd3d_url" "$vkd3d_commit"
  submodules "$work/vkd3d-proton"
  # dxil-spirv is bumped past vkd3d-proton's gitlink: the newer translator
  # fixes invalid SPIR-V that crashed the host driver (Godot D3D12).
  local dxil="$work/vkd3d-proton/subprojects/dxil-spirv"
  fetch "$dxil" "$dxil_url" "$dxil_commit"
  submodules "$dxil"
  apply "$dxil" "$repo/patches/dxil-spirv"
  apply "$work/vkd3d-proton" "$repo/patches/vkd3d-proton"

  fetch "$work/dxvk" "$dxvk_url" "$dxvk_commit"
  submodules "$work/dxvk"
  apply "$work/dxvk" "$repo/patches/dxvk"

  echo "$stamp" > "$work/stamp"
}

if [[ "$(cat "$work/stamp" 2>/dev/null)" != "$stamp" ]]; then
  prepare_sources
else
  echo "== sources match stamp $stamp; building incrementally"
fi

# Static C++ runtime so the DLLs import only Windows system DLLs.
static_link="['-static', '-static-libgcc', '-static-libstdc++']"

for arch in x86_64 i686; do
  case "$arch" in
    x86_64) dir=x64 ;;
    i686) dir=x86 ;;
  esac
  cross=(--cross-file "/opt/cross/meson-$arch.ini" --cross-file "$repo/build/d3d12/cross/widl-$arch.ini")

  b="$work/build/vkd3d-$arch"
  if [[ ! -f "$b/build.ninja" ]]; then
    meson setup "$b" "$work/vkd3d-proton" "${cross[@]}" \
      --buildtype release --strip -Denable_tests=false -Denable_extras=false \
      -Dc_link_args="$static_link" -Dcpp_link_args="$static_link"
  fi
  ninja -C "$b" -j "$jobs"

  b="$work/build/dxvk-$arch"
  if [[ ! -f "$b/build.ninja" ]]; then
    meson setup "$b" "$work/dxvk" "${cross[@]}" \
      --buildtype release --strip \
      -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false -Denable_d3d11=false \
      -Dc_link_args="$static_link" -Dcpp_link_args="$static_link"
  fi
  ninja -C "$b" -j "$jobs"

  mkdir -p "$out/$dir"
  install -m 0644 "$work/build/vkd3d-$arch/libs/d3d12/d3d12.dll" "$out/$dir/d3d12.dll"
  install -m 0644 "$work/build/vkd3d-$arch/libs/d3d12core/d3d12core.dll" "$out/$dir/vkd3dcore.dll"
  install -m 0644 "$work/build/dxvk-$arch/src/dxgi/dxgi.dll" "$out/$dir/dxgi-vulkan.dll"
done

bash "$repo/build/d3d12/check.sh" "$out"
