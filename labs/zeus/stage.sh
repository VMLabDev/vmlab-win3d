#!/usr/bin/env bash
# Copy what labs/zeus/run.sh needs from helios to Zeus's staging tree
# (~/win3d-zeus/repo), laid out like this repo, and fetch its evidence back.
#
#   labs/zeus/stage.sh [E2E_TREE]     push: out/package, out/probes, tests/guest,
#                                     scripts/install-into-vm.sh, installer/VERSION,
#                                     labs/zeus, patches/host, and tests/e2e
#                                     from E2E_TREE (default: the work/e2e worktree)
#   labs/zeus/stage.sh --fetch GPU    pull docs/evidence/zeus/GPU back
#
# Build the package first with scripts/build.sh. Set ZEUS to another ssh host.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
ZEUS=${ZEUS:-zeus}
DEST=win3d-zeus/repo
if [ "${1:-}" = --fetch ]; then
    mkdir -p "$REPO/docs/evidence/zeus/$2"
    rsync -a --delete "$ZEUS:$DEST/docs/evidence/zeus/$2/" "$REPO/docs/evidence/zeus/$2/"
    exit 0
fi
E2E=${1:-$(cd "$REPO/../e2e" && pwd)}
ssh "$ZEUS" "mkdir -p $DEST/out $DEST/tests $DEST/scripts $DEST/installer $DEST/labs $DEST/patches"
rsync -a --delete "$REPO/out/package/" "$ZEUS:$DEST/out/package/"
rsync -a --delete "$REPO/out/probes/" "$ZEUS:$DEST/out/probes/"
rsync -a --delete "$REPO/tests/guest/" "$ZEUS:$DEST/tests/guest/"
rsync -a --delete --exclude __pycache__ "$E2E/tests/e2e/" "$ZEUS:$DEST/tests/e2e/"
rsync -a "$REPO/scripts/install-into-vm.sh" "$ZEUS:$DEST/scripts/"
rsync -a "$REPO/installer/VERSION" "$ZEUS:$DEST/installer/"
rsync -a --delete "$REPO/labs/zeus/" "$ZEUS:$DEST/labs/zeus/"
rsync -a --delete "$REPO/patches/host/" "$ZEUS:$DEST/patches/host/"
{
    echo "repo $(git -C "$REPO" rev-parse --short HEAD)$(git -C "$REPO" diff --quiet HEAD -- labs/zeus || echo -dirty)"
    echo "e2e $(git -C "$E2E" rev-parse --short HEAD) ($E2E)"
    echo "package $(python3 -c 'import json,glob; print(json.load(open(glob.glob("'"$REPO"'/out/package/*/manifest.json")[0]))["commit"])')"
} | ssh "$ZEUS" "cat > $DEST/STAGED"
ssh "$ZEUS" "cat $DEST/STAGED"
