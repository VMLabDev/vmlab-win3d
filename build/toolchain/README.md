# toolchain

The shared Docker image for every Windows cross build in this repository.

It holds llvm-mingw 20261006 (LLVM 23.1.3, ucrt) for `x86_64-w64-mingw32` and
`i686-w64-mingw32` in `/opt/llvm-mingw`, plus Meson, Mako, PyYAML and
packaging in a venv at `/opt/py`, and CMake, Ninja, bison, flex, glslang, git
and curl from an Ubuntu 24.04 archive snapshot. Every input is pinned in
`PINS`.

## Use

```sh
image="$(build/toolchain/build.sh)"   # builds once, then reuses the tag
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/w" -w /w "$image" ...
```

`build.sh` prints `vmlab-win3d/toolchain:<hash>`. The hash covers
`Dockerfile`, `requirements.txt` and `cross/`, so any change to the image
inputs gives a new tag. `tag.sh` prints the tag without building.
`build.sh --force` rebuilds an existing tag.

The image runs as any uid. `HOME` is `/tmp`, and git treats every directory
as safe.

## Cross files

| File | Use |
| --- | --- |
| `/opt/cross/meson-x86_64.ini`, `/opt/cross/meson-i686.ini` | Meson `--cross-file` |
| `/opt/cross/cmake-x86_64.cmake`, `/opt/cross/cmake-i686.cmake` | CMake `-DCMAKE_TOOLCHAIN_FILE` |

The Meson cross files set `pkg-config = 'false'`, so a cross build does not pick
up host libraries.
