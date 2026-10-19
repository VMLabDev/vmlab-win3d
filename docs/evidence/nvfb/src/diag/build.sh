#!/usr/bin/env bash
# Build a diagnostic bundle: this branch's package plus extra Mesa patches
# (default: 0099-venus-nvfb-memlog.patch here), in a scratch copy of the
# tree, so that out/package stays the product build. Run scripts/build.sh
# first; the copy reuses out/<component> and rebuilds only Mesa.
#
#   docs/evidence/nvfb/src/diag/build.sh <name> [extra.patch ...] [-<patch to drop> ...]
#
# Output: .scratch/diag-<name>/out/package/vmlab-win3d-<version>/.
# "-0065" drops patches/mesa/0065-* from the copy (used for the 0066-only
# experiment in docs/nvfb.md).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../../.." && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
NAME=$1; shift
X=$REPO/.scratch/diag-$NAME
[ $# -gt 0 ] || set -- "$HERE/0099-venus-nvfb-memlog.patch"
rm -rf "$X"; mkdir -p "$X/out" "$X/.scratch"
git -C "$REPO" archive HEAD | tar -x -C "$X"
for a in "$@"; do
    case $a in
        -*) rm -v "$X"/patches/mesa/"${a#-}"-* ;;
        *) cp -v "$a" "$X/patches/mesa/" ;;
    esac
done
for d in "$REPO"/out/*; do [ "$(basename "$d")" = package ] || cp -a "$d" "$X/out/"; done
[ ! -d "$REPO/.scratch/cache" ] || cp -a "$REPO/.scratch/cache" "$X/.scratch/"
cd "$X"
build/mesa/build.sh
scripts/build.sh --package-only
