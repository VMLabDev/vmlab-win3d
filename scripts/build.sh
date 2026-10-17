#!/usr/bin/env bash
# Build the whole vmlab-win3d package from a clean checkout.
#
#   scripts/build.sh [--package-only]
#
# Runs every component build, then assembles and test-signs the install
# bundle into out/package/ (build/package/build.sh). --package-only skips the
# component builds and reuses out/<component>/.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO"

stage() { printf '\n==== %s (%s)\n' "$1" "$(date -Is)"; }

if [ "${1:-}" != --package-only ]; then
    stage toolchain;  build/toolchain/build.sh
    stage mesa;       build/mesa/build.sh
    stage vulkan-sdk; build/vulkan-sdk/build.sh
    stage d3d12;      build/d3d12/build.sh
    stage kmd;        build/kmd/build.sh
    stage probes;     build/probes/build.sh
fi

stage signing-cert
build/signing/make-test-cert.sh

stage package
build/package/build.sh
