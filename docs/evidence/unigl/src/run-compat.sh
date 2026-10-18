#!/usr/bin/env bash
# Run tests/apps/compat/run.sh under this worker's own lab and evidence names,
# so that it does not collide with other workers' win3d-compat-<gpu> labs.
#
#   docs/evidence/unigl/src/run-compat.sh TAG [run.sh options...]
#
# The lab is win3d-unigl-<tag>-<gpu>, its working files are under
# .scratch/unigl-compat/<tag>/<gpu>/, and the evidence goes to
# docs/evidence/unigl/<evidence>/<gpu>/, where <evidence> is $UNIGL_EV or
# else the tag (so one lab can serve a baseline and a fixed run). Everything
# else is run.sh unchanged.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
TAG=${1:?usage: run-compat.sh TAG [run.sh options]}; shift
[[ $TAG =~ ^[a-z0-9-]+$ ]] || { echo "bad tag '$TAG'" >&2; exit 2; }
EVT=${UNIGL_EV:-$TAG}
[[ $EVT =~ ^[a-z0-9-]+$ ]] || { echo "bad UNIGL_EV '$EVT'" >&2; exit 2; }
T=$REPO/.scratch/unigl-compat/$TAG/bin
mkdir -p "$T" "$REPO/.scratch/compat/cache"
cp "$REPO"/tests/apps/compat/{apps.sh,Close-App.ps1,PINS} "$T/"
sed -e "s|^REPO=.*|REPO=$REPO|" \
    -e "s|^HERE=.*|HERE=$T|" \
    -e "s|^LAB=win3d-compat-\$GPU|LAB=win3d-unigl-$TAG-\$GPU|" \
    -e "s|^WORK=.*|WORK=\$REPO/.scratch/unigl-compat/$TAG/\$GPU|" \
    -e "s|^EV=.*|EV=\$REPO/docs/evidence/unigl/$EVT/\$GPU|" \
    "$REPO/tests/apps/compat/run.sh" >"$T/run.sh"
grep -q "^LAB=win3d-unigl-" "$T/run.sh" && grep -q "^EV=.*evidence/unigl" "$T/run.sh" ||
    { echo "run.sh changed shape; update $0" >&2; exit 1; }
# The big downloads, if the shared cache has them.
for f in /home/wil/dev/vmlab-win3d/.scratch/shared-cache/*; do
    [ -f "$f" ] && [ ! -f "$REPO/.scratch/compat/cache/${f##*/}" ] && cp "$f" "$REPO/.scratch/compat/cache/"
done
exec bash "$T/run.sh" "$@"
