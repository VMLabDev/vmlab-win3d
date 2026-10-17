# Sourced by make-test-cert.sh and sign-driver.sh. Builds the signing image
# and defines run_tool, which runs a command in it as the calling user.
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
CACHE=${WIN3D_CACHE:-$REPO/.scratch/cache}
SIGNING_DIR=$REPO/.signing
IMAGE=vmlab-win3d/signing:1

_pin() { awk -v n="$1" '$1 == n { print $'"$2"' }' "$HERE/PINS"; }

build_tools_image() {
    local url rev repo ctx
    url=$(_pin generate-cat-file 3); rev=$(_pin generate-cat-file 4)
    repo=$CACHE/git/generate-cat-file.git
    [ -d "$repo" ] || git clone -q --bare "$url" "$repo"
    git -C "$repo" cat-file -e "$rev^{commit}" 2>/dev/null || git -C "$repo" fetch -q origin "$rev"
    ctx=$CACHE/signing-ctx
    rm -rf "$ctx"; mkdir -p "$ctx/generate-cat-file"
    git -C "$repo" archive "$rev" | tar -x -C "$ctx/generate-cat-file"
    cp "$HERE/Dockerfile" "$ctx/"
    docker build -q -t "$IMAGE" "$ctx" >/dev/null
}

run_tool() { # run_tool [docker args...] -- command...
    local dargs=()
    while [ "$1" != "--" ]; do dargs+=("$1"); shift; done; shift
    docker run --rm --network none --user "$(id -u):$(id -g)" \
        -v "$HERE:/signing-scripts:ro" "${dargs[@]}" "$IMAGE" "$@"
}
