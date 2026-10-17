#!/usr/bin/env bash
# Assemble and test-sign the install bundle from the component outputs.
#
#   build/package/build.sh
#
# Needs out/{kmd,mesa,vulkan-sdk,d3d12}/ (scripts/build.sh builds them) and
# the test certificate in .signing/ (build/signing/make-test-cert.sh).
# Writes out/package/:
#   vmlab-win3d-<version>/        the bundle (layout in docs/install.md)
#   vmlab-win3d-<version>.zip     the same, zipped with fixed timestamps
#   SHA256SUMS
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/out/package
VERSION=$(cat "$REPO/installer/VERSION")
NAME=vmlab-win3d-$VERSION
B=$OUT/$NAME

need() { for f in "$@"; do [ -f "$REPO/$f" ] || { echo "missing $f; run scripts/build.sh" >&2; exit 1; }; done; }
need out/kmd/viogpu3d_x64_wow64.inf.template out/kmd/x64/viogpu3d.sys out/kmd/x64/vgpusrv.exe out/kmd/x64/viogpuap.exe \
     .signing/test-signing.cer
for a in x64 x86; do
    need out/mesa/$a/viogpu_d3d10.dll out/mesa/$a/viogpu_wgl.dll out/mesa/$a/libvulkan_virtio.dll out/mesa/$a/virtio_icd.json \
         out/vulkan-sdk/$a/vulkan-1.dll out/vulkan-sdk/$a/vulkaninfo.exe \
         out/d3d12/$a/d3d12.dll out/d3d12/$a/vkd3dcore.dll out/d3d12/$a/dxgi-vulkan.dll
done

rm -rf "$OUT"
mkdir -p "$B/driver/x64" "$B/driver/x86" "$B/runtime/x64" "$B/runtime/x86"

echo "== driver package"
cp "$REPO"/out/kmd/x64/{viogpu3d.sys,vgpusrv.exe,viogpuap.exe} "$B/driver/"
python3 "$HERE/make-inf.py" "$REPO/out/kmd/viogpu3d_x64_wow64.inf.template" "$B/driver/viogpu3d.inf"
for a in x64 x86; do
    for f in viogpu_d3d10 viogpu_wgl libvulkan_virtio; do
        cp "$REPO/out/mesa/$a/$f.dll" "$B/driver/$a/${f}_$a.dll"
    done
done
# The INF names the ICD manifests by architecture; each is installed next
# to its DLL, and its library_path is relative to it.
cp "$REPO/out/mesa/x64/virtio_icd.json" "$B/driver/x64/virtio_icd.x86_64.json"
cp "$REPO/out/mesa/x86/virtio_icd.json" "$B/driver/x86/virtio_icd.x86.json"
python3 "$HERE/check-inf.py" "$B/driver/viogpu3d.inf"
"$REPO/build/signing/sign-driver.sh" "$B/driver"

echo "== runtime"
for a in x64 x86; do
    cp "$REPO"/out/vulkan-sdk/$a/{vulkan-1.dll,vulkaninfo.exe} "$B/runtime/$a/"
    cp "$REPO"/out/d3d12/$a/{d3d12.dll,vkd3dcore.dll,dxgi-vulkan.dll} "$B/runtime/$a/"
done

echo "== installer"
cp "$REPO/installer/vmlab-win3d.ps1" "$B/"
# cmd.exe needs CRLF line endings.
for f in Install.cmd Uninstall.cmd; do sed 's/\r*$/\r/' "$REPO/installer/$f" >"$B/$f"; done
sed 's/\r*$/\r/' "$REPO/installer/README.txt" >"$B/README.txt"
cp "$REPO/.signing/test-signing.cer" "$B/vmlab-win3d-test.cer"

echo "== manifest and zip"
COMMIT=$(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)
[ -z "$(git -C "$REPO" status --porcelain -- installer build scripts 2>/dev/null)" ] || COMMIT="$COMMIT-dirty"
python3 "$HERE/manifest.py" "$B" "$VERSION" "$COMMIT" "$OUT/$NAME.zip"
(cd "$OUT" && sha256sum "$NAME.zip" "$NAME/manifest.json" >SHA256SUMS)
cat "$OUT/SHA256SUMS"
echo "bundle: $B"
