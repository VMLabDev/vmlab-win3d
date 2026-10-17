#!/usr/bin/env bash
# Build the shared toolchain image and print its tag.
# Tag: vmlab-win3d/toolchain:<first 12 hex of sha256 over the image inputs>.
# Other build scripts call this; it is a no-op when the tag already exists.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tag="$("$here/tag.sh")"
if [[ "${1:-}" != "--force" ]] && docker image inspect "$tag" >/dev/null 2>&1; then
  echo "$tag"
  exit 0
fi
DOCKER_BUILDKIT=1 docker build --tag "$tag" "$here" >&2
echo "$tag"
