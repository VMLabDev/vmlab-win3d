# Sourced by patches/host/build-qemu.sh and build-virglrenderer.sh: run the
# calling script inside the Debian 12 build container (patches/host/Dockerfile)
# with the repository mounted at its own path, so paths mean the same on both
# sides. The caller sets HERE (patches/host) and REPO before sourcing.
#
#   in_container "$0" "$@"    re-executes the script in the container and
#                             returns its status; inside the container, where
#                             WIN3D_IN_HOST_BUILD is set, it returns 0 at once
#
# Docker runs without sudo. The container runs as the calling user, writes
# only under the mounted repository (out/ and .scratch/), and has network
# access for the subprojects QEMU's configure downloads. The image tag is
# vmlab-win3d/host-build:<12 hex of sha256 over the Dockerfile and the base
# image digest from PINS>; a missing image is built first.

host_build_image() {
    local digest tag py_url py_sha
    digest=$(awk '$1 == "debian-bookworm" && $3 == "digest" { print $4 }' "$HERE/PINS")
    [[ $digest =~ ^sha256:[0-9a-f]{64}$ ]] || { echo "error: no debian-bookworm digest line in $HERE/PINS" >&2; return 1; }
    read -r py_url py_sha < <(awk '$1 == "python" && $3 == "version" && $5 == "sha256" { print $2, $6 }' "$HERE/PINS")
    [[ ${py_sha:-} =~ ^[0-9a-f]{64}$ ]] || { echo "error: no python line in $HERE/PINS" >&2; return 1; }
    tag=vmlab-win3d/host-build:$({ cat "$HERE/Dockerfile"; echo "$digest $py_url $py_sha"; } | sha256sum | cut -c1-12)
    if ! docker image inspect "$tag" >/dev/null 2>&1; then
        echo "building the image $tag from debian@$digest" >&2
        DOCKER_BUILDKIT=1 docker build --build-arg "BASE=debian@$digest" \
            --build-arg "PYTHON_URL=$py_url" --build-arg "PYTHON_SHA256=$py_sha" --tag "$tag" \
            --file "$HERE/Dockerfile" "$HERE" >&2 || return 1
    fi
    echo "$tag"
}

in_container() {
    [ -z "${WIN3D_IN_HOST_BUILD:-}" ] || return 0
    local image script=$1; shift
    image=$(host_build_image) || exit 1
    HOST_BUILD_IMAGE=$image
    HOST_BUILD_IMAGE_ID=$(docker image inspect --format '{{.Id}}' "$image")
    local envs=(-e WIN3D_IN_HOST_BUILD=1 -e "HOME=$REPO/.scratch/host-build-home" -e "HOST_BUILD_IMAGE=$image" -e "HOST_BUILD_IMAGE_ID=$HOST_BUILD_IMAGE_ID")
    local v
    for v in WITH_WORKAROUND QEMU_URL QEMU_COMMIT; do
        [ -z "${!v:-}" ] || envs+=(-e "$v=${!v}")
    done
    mkdir -p "$REPO/.scratch/host-build-home"
    docker run --rm --init -u "$(id -u):$(id -g)" "${envs[@]}" \
        -v "$REPO:$REPO" -w "$REPO" "$image" "$script" "$@"
}
