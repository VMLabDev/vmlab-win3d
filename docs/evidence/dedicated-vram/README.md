# Dedicated-memory prerequisite investigation

## Result

Dedicated-memory reporting is not implemented or accepted. The independent
allocator fix builds and passes focused host checks. No Windows VM was started,
and none of the requested native checks ran. Package 0.1.4 was not assembled or
versioned. `docs/dedicated-vram.md` explains the missing cross-layer residency
contract that prevents a correct KMD-only conversion.

## Allocator reproduction

Run from this worktree with the pinned exported KMD source as `SOURCE`:

```sh
bash tests/kmd/run-shmem-tests.sh "$SOURCE" --baseline
bash tests/kmd/run-shmem-tests.sh "$SOURCE"
CXX=clang++ bash tests/kmd/run-shmem-tests.sh "$SOURCE"
```

`allocator-baseline.txt` records exit 1 and three expected failures. The
unchanged allocator caps 32 GiB at 16 GiB, rejects a 20 GiB range, and accepts an
overflowing request after page rounding produces zero. `allocator-patched.txt`
and `allocator-clang.txt` record exit 0 with all nine checks passing, using GCC
and Clang respectively. Both runs enable AddressSanitizer and
UndefinedBehaviorSanitizer. These checks allocate metadata, not GPU memory.

## Driver build

```sh
WIN3D_CACHE=/home/wil/dev/vmlab-win3d/.scratch/shared-cache build/kmd/build.sh
```

The full KMD build exited 0 after applying patches 0001 through 0005. It produced
the driver, helpers, and INF files. `kmd-BUILDINFO.txt` records inputs, patch
hashes, and output hashes. The driver version remains `100.6.101.58104` because
this is not an accepted dedicated-memory package.

Documentation path checks and generated-pin checks also pass. Shell syntax
validation passes for the test runner. The repository has no root lint or
typecheck command. No claim is made about native kernel execution, concurrent
allocator behavior, GPU contents above 4 GiB, or TDR after this change.

## Pending native acceptance

- A1: DXGI, dxdiag, and Win32_VideoController at default VRAM and 16 GiB.
- A2: D3D9/11/12, OpenGL, and Vulkan readback for x64 and x86.
- A3: Forced TDR with reset/restart telemetry and post-reset hardware readback.
- A4: More than 4 GiB of live GPU allocations with verified contents.
- A5: Dirty eviction, page-in, fragmentation, and reset during paging.

The package version bump and `w60-win3d-vram done` event remain pending because
those acceptance criteria have not been met. No temporary VM, daemon, or host
display window was created.
