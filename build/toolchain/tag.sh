#!/usr/bin/env bash
# Print the toolchain image tag. It changes whenever an image input changes.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
hash="$(find Dockerfile requirements.txt cross -type f | LC_ALL=C sort \
  | xargs sha256sum | sha256sum | cut -c1-12)"
echo "vmlab-win3d/toolchain:$hash"
