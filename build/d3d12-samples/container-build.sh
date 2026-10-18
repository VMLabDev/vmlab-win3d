#!/usr/bin/env bash
# Runs inside the shared toolchain image (build/toolchain); started by build.sh.
# /repo is this repository (read-only), /out is out/d3d12-samples (read-write).
#
# /out/.work keeps the checked downloads, the samples checkout and the
# splatted CRT/SDK (/out/.work/winsysroot), so a rerun fetches nothing.
set -euo pipefail

repo=/repo
here=$repo/build/d3d12-samples
out=/out
work=$out/.work
dl=$work/downloads
mkdir -p "$dl"

pin() { awk -v n="$1" '$1 == n { print $2, $3, $4 }' "$here/PINS"; }

# fetch <name>: download a pinned archive once and check its sha256.
fetch() {
  local url ref sha f
  read -r url ref sha < <(pin "$1")
  f=$dl/$1-${url##*/}
  if [[ ! -f $f ]]; then
    echo "fetch $1 $ref" >&2
    curl -sSfL --retry 5 -o "$f.part" "$url"
    mv "$f.part" "$f"
  fi
  echo "$sha  $f" | sha256sum -c --quiet - >&2
  echo "$f"
}

# tree_hash <dir>: sha256 over every entry's path, type and content (a
# symlink's target), so the splat is checked as a whole.
tree_hash() {
  (cd "$1" && find . -mindepth 1 \( -type f -printf 'f %p ' -exec sha256sum {} \; \) \
      -o \( -type l -printf 'l %p %l\n' \) -o \( -type d -printf 'd %p\n' \) \
    | LC_ALL=C sort | sha256sum | cut -c1-64)
}

# --- tools -----------------------------------------------------------------
tools=$work/tools
if [[ ! -x $tools/xwin || ! -x $tools/dxc/bin/dxc ]]; then
  rm -rf "$tools"; mkdir -p "$tools/dxc"
  tar -C "$tools" --strip-components=1 -xzf "$(fetch xwin)" --wildcards '*/xwin'
  tar -C "$tools/dxc" -xzf "$(fetch dxc)"
fi

# --- MSVC CRT and Windows SDK ------------------------------------------------
read -r _ crt_version _ < <(pin crt-version)
read -r _ sdk_version _ < <(pin sdk-version)
read -r _ _ sysroot_sha < <(pin winsysroot)
sysroot=$work/winsysroot
if [[ ! -f $sysroot.ok ]]; then
  rm -rf "$sysroot"
  # xwin reads the package manifest from its cache when it is there, under
  # the name it would download it to; the channel manifest it starts from is
  # written here with only that one entry, so nothing comes from the moving
  # aka.ms channel.
  read -r vsman_url vsman_build _ < <(pin vsman)
  url_sha=$(basename "$(dirname "$vsman_url")")
  mkdir -p "$work/xwin-cache/dl"
  cp "$(fetch vsman)" "$work/xwin-cache/dl/pkg_manifest_$url_sha.vsman"
  printf '{"channelItems":[{"id":"Microsoft.VisualStudio.Manifests.VisualStudio","version":"%s","type":"Manifest","payloads":[{"fileName":"VisualStudio.vsman","sha256":"%s","size":0,"url":"%s"}]}]}\n' \
    "$vsman_build" "$url_sha" "$vsman_url" > "$work/channel.json"
  echo "== xwin splat: CRT $crt_version, SDK $sdk_version" >&2
  xwin=("$tools/xwin" --accept-license --cache-dir "$work/xwin-cache" --manifest "$work/channel.json"
        --crt-version "$crt_version" --sdk-version "$sdk_version" --arch x86_64 --variant desktop
        --timeout 3600s --http-retry 10)
  for try in 1 2 3 4 5; do "${xwin[@]}" download && break; [[ $try == 5 ]] && exit 1; sleep 10; done
  "${xwin[@]}" splat --use-winsysroot-style --preserve-ms-arch-notation --output "$sysroot"
  got=$(tree_hash "$sysroot")
  if [[ $got != "$sysroot_sha" ]]; then
    echo "winsysroot tree hash $got does not match PINS ($sysroot_sha)" >&2
    exit 1
  fi
  touch "$sysroot.ok"
fi

# --- NuGet packages ------------------------------------------------------------
for p in nuget-d3d12 nuget-pix; do
  if [[ ! -d $work/$p ]]; then
    mkdir -p "$work/$p.tmp"
    (cd "$work/$p.tmp" && unzip -q "$(fetch $p)")
    mv "$work/$p.tmp" "$work/$p"
  fi
done

# --- the samples ----------------------------------------------------------------
read -r samples_url _ samples_commit < <(pin samples)
src=$work/samples
if [[ ! -d $src/.git ]]; then
  git init -q "$src"
  git -C "$src" remote add origin "$samples_url"
fi
git -C "$src" sparse-checkout set --no-cone /Samples/Desktop/D3D12HelloWorld/src/HelloTriangle \
  /Samples/Desktop/D3D12HelloWorld/src/HelloTexture /Samples/Desktop/D3D12Bundles/src \
  /Samples/Desktop/D3D12nBodyGravity/src /Samples/Desktop/D3D12ExecuteIndirect/src
if ! git -C "$src" cat-file -e "$samples_commit^{commit}" 2>/dev/null; then
  echo "fetch samples $samples_commit" >&2
  git -C "$src" fetch -q --depth 1 --filter=blob:none origin "$samples_commit"
fi
git -C "$src" checkout -q -f --detach "$samples_commit"
git -C "$src" clean -qffdx

rm -rf "$out/x64"
python3 "$here/build-samples.py" --src "$src" --sysroot "$sysroot" \
  --dxc "$tools/dxc/bin/dxc" --agility "$work/nuget-d3d12" --pix "$work/nuget-pix" \
  --out "$out/x64" --jobs "${JOBS:-4}"

(cd "$out" && find x64 -type f | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
echo "== done: $out/x64" >&2
