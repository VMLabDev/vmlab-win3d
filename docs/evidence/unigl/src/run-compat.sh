#!/usr/bin/env bash
# Run tests/apps/compat/run.sh under this worker's own lab and evidence names,
# so that it does not collide with other workers' win3d-compat-<gpu> labs.
#
#   docs/evidence/unigl/src/run-compat.sh TAG --host-gpu NAME [run.sh options...]
#
# The lab is win3d-unigl-<tag>-<gpu> and the evidence goes to
# docs/evidence/unigl/<evidence>/<gpu>/, where <evidence> is $UNIGL_EV or
# else the tag (so one lab can serve several runs), through run.sh's
# COMPAT_LAB and COMPAT_EV. Everything else is run.sh unchanged.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
TAG=${1:?usage: run-compat.sh TAG --host-gpu NAME [run.sh options]}; shift
[[ $TAG =~ ^[a-z0-9-]+$ ]] || { echo "bad tag '$TAG'" >&2; exit 2; }
EVT=${UNIGL_EV:-$TAG}
[[ $EVT =~ ^[a-z0-9-]+$ ]] || { echo "bad UNIGL_EV '$EVT'" >&2; exit 2; }
GPU=
args=("$@")
for ((i = 0; i < ${#args[@]}; i++)); do
    [ "${args[$i]}" = --host-gpu ] && GPU=${args[$((i + 1))]:-}
done
[ -n "$GPU" ] || { echo "run-compat.sh needs --host-gpu" >&2; exit 2; }
GPU=${GPU,,}
export COMPAT_LAB=win3d-unigl-$TAG-$GPU
export COMPAT_EV=$REPO/docs/evidence/unigl/$EVT/$GPU
# The big downloads, if the shared cache has them.
mkdir -p "$REPO/.scratch/compat/cache"
for f in /home/wil/dev/vmlab-win3d/.scratch/shared-cache/*; do
    [ -f "$f" ] && [ ! -f "$REPO/.scratch/compat/cache/${f##*/}" ] && cp "$f" "$REPO/.scratch/compat/cache/"
done
exec "$REPO/tests/apps/compat/run.sh" "$@"
