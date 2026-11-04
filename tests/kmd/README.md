# Shared-memory allocator regression

Run against the source directory exported from the commit in `build/kmd/PINS`:

```sh
bash tests/kmd/run-shmem-tests.sh /path/to/yttrium-source --baseline
bash tests/kmd/run-shmem-tests.sh /path/to/yttrium-source
```

The baseline command runs unchanged upstream code. It must fail the 32 GiB
capacity check, the 20 GiB allocation, and the page-rounding overflow rejection.
The second command applies patch 0005 and must pass every check.

The runner compiles the actual allocator source and header. A host-only helper
substitutes list, pool, and lock primitives. AddressSanitizer and
UndefinedBehaviorSanitizer check its range operations. No GPU memory is allocated.
The helper does not model kernel concurrency or allocation failures.

The test covers capacity above 16 GiB, aggregate allocation above 4 GiB,
exhaustion, coalescing, unaligned region tails, rounding overflow, and sub-page
regions. It does not prove WDDM paging, BAR mapping, rendering, or TDR recovery.
