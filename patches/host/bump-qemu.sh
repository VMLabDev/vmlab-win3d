#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Move the QEMU pin in patches/host/PINS to the tip of its fork branch.
#
#   patches/host/bump-qemu.sh [commit]
#
# Resolves the tip of the branch on the "qemu" line of patches/host/PINS
# with git ls-remote (or takes the given full commit), rewrites that line's
# commit, and prints the old and new commits with the commits between them
# (each side's series, down to its upstream merge, when the branch was
# rewritten). It changes nothing else:
# rebuild with patches/host/build-qemu.sh, test, and commit PINS (and
# docs/stack.md, scripts/pins-table.py --write) yourself.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
PINS=$HERE/PINS
CACHE=${WIN3D_CACHE:-$REPO/.scratch/cache}/git/qemu-vmlab.git

read -r URL BRANCH OLD < <(awk '$1 == "qemu" && $3 == "branch" && $5 == "commit" { print $2, $4, $6 }' "$PINS")
[ -n "${OLD:-}" ] || { echo "no qemu branch/commit pin in $PINS" >&2; exit 1; }
if [ $# -ge 1 ]; then
    NEW=$1
else
    NEW=$(git ls-remote "$URL" "refs/heads/$BRANCH" | cut -f1)
fi
[[ $NEW =~ ^[0-9a-f]{40}$ ]] || { echo "cannot resolve $URL $BRANCH (got '$NEW')" >&2; exit 1; }

echo "branch: $URL $BRANCH"
echo "old:    $OLD"
echo "new:    $NEW"
if [ "$OLD" = "$NEW" ]; then echo "PINS is already at the branch tip"; exit 0; fi

# Commit metadata only (no blobs), enough history for the series and its base.
[ -d "$CACHE" ] || git init -q --bare "$CACHE"
git -C "$CACHE" fetch -q --filter=blob:none --depth 300 "$URL" "$NEW" "$OLD" 2>/dev/null \
    || git -C "$CACHE" fetch -q --filter=blob:none --depth 300 "$URL" "$NEW"
if git -C "$CACHE" cat-file -e "$OLD^{commit}" 2>/dev/null; then
    if git -C "$CACHE" merge-base --is-ancestor "$OLD" "$NEW"; then
        echo; echo "commits added (old..new):"
        git -C "$CACHE" log --oneline "$OLD..$NEW"
    else
        # A rebase onto a newer upstream puts thousands of upstream commits
        # between the two, so print each side's own series instead.
        series() { git -C "$CACHE" log --first-parent --format='%h %p%x09%s' "$1" | awk -F'\t' '!done { if (split($1, f, " ") > 2) { print "  on " f[1]; done = 1 } else if (++n > 20) { print "  ..."; done = 1 } else print "  " f[1], $2 }'; }
        echo; echo "the branch was rewritten. old series:"; series "$OLD"
        echo "new series:"; series "$NEW"
    fi
else
    echo; echo "old commit is no longer on the remote; the new branch's latest commits:"
    git -C "$CACHE" log --oneline -10 "$NEW"
fi

sed -i "/^qemu  *[^ ]*  *branch  *[^ ]*  *commit /s/$OLD/$NEW/" "$PINS"
grep -q "commit $NEW" "$PINS" || { echo "failed to rewrite $PINS" >&2; exit 1; }
echo; echo "rewrote $PINS: $OLD -> $NEW"
echo "next: patches/host/build-qemu.sh, test on an NVIDIA GPU, scripts/pins-table.py --write, commit"
