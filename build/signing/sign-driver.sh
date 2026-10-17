#!/usr/bin/env bash
# Test-sign a driver package directory in place.
#
#   build/signing/sign-driver.sh <driver-dir>
#
# <driver-dir> holds exactly one .inf plus the files it installs (subdirectories
# allowed). The script:
#   1. embeds an Authenticode SHA-256 signature in every PE file (.sys .exe
#      .dll), replacing any signature already there;
#   2. writes the catalog named by the INF's CatalogFile= line, listing every
#      file in the directory except .cat and .pdb, with the INF's hardware IDs
#      and OS attribute 2:10.0 (_v100_X64);
#   3. signs the catalog;
#   4. verifies every signature against the test certificate.
# The key and certificate come from .signing/ (build/signing/make-test-cert.sh).
# Signing time and catalog GUID are derived from fixed inputs, so running it
# twice on the same directory leaves the bytes unchanged.
set -euo pipefail
source "$(dirname "$0")/tools.sh"

[ $# -eq 1 ] && [ -d "$1" ] || { echo "usage: $0 <driver-dir>" >&2; exit 2; }
DIR=$(cd "$1" && pwd)

for f in test-signing.key test-signing.crt SIGN_TIME; do
    [ -f "$SIGNING_DIR/$f" ] || { echo "missing $SIGNING_DIR/$f; run build/signing/make-test-cert.sh" >&2; exit 1; }
done

shopt -s nullglob
infs=("$DIR"/*.inf)
[ ${#infs[@]} -eq 1 ] || { echo "expected exactly one .inf in $DIR" >&2; exit 1; }
INF=${infs[0]}

build_tools_image
run_tool -v "$SIGNING_DIR:/s:ro" -v "$DIR:/pkg" -w /pkg -- \
    bash /signing-scripts/sign-inner.sh "/pkg/$(basename "$INF")"
