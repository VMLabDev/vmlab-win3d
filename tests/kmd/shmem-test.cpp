#include "viogpu_shmem_allocator.h"

#include <cstdio>
#include <limits>

static unsigned failures;
static void check(bool result, const char *name)
{
    std::printf("%s %s\n", result ? "PASS" : "FAIL", name);
    failures += !result;
}

int main()
{
    constexpr ULONGLONG GiB = 1ull << 30;
    VioGpuShmemAllocator allocator;
    ULONGLONG offset = 0;

    allocator.Init(32 * GiB);
    check(allocator.GetTotal() == 32 * GiB, "capacity follows a 32 GiB region");
    check(allocator.Allocate(20 * GiB, PAGE_SIZE, &offset) && offset == 0,
          "one allocation exceeds the old 16 GiB clamp");
    allocator.Reset();

    allocator.Init(16 * GiB);
    bool first = allocator.Allocate(5 * GiB, PAGE_SIZE, &offset) && offset == 0;
    bool second = allocator.Allocate(11 * GiB, PAGE_SIZE, &offset) && offset == 5 * GiB;
    check(first && second, "aggregate allocations cross 4 GiB and fill the region");
    check(!allocator.Allocate(PAGE_SIZE, PAGE_SIZE, &offset), "full region rejects allocation");
    allocator.Free(0, 5 * GiB);
    allocator.Free(5 * GiB, 11 * GiB);
    check(allocator.Allocate(16 * GiB, PAGE_SIZE, &offset) && offset == 0,
          "freed ranges coalesce across 4 GiB");
    allocator.Reset();

    allocator.Init(8 * GiB + PAGE_SIZE - 1);
    check(allocator.GetTotal() == 8 * GiB, "region tail rounds down to a page");
    check(!allocator.Allocate(std::numeric_limits<ULONGLONG>::max(), PAGE_SIZE, &offset),
          "page-rounding overflow rejects allocation");
    check(allocator.Allocate(8 * GiB, PAGE_SIZE, &offset) && offset == 0,
          "rejected overflow leaves capacity intact");
    allocator.Reset();

    allocator.Init(PAGE_SIZE - 1);
    check(allocator.GetTotal() == 0 && !allocator.Allocate(1, 0, &offset),
          "sub-page region has no capacity");
    allocator.Reset();
    return failures ? 1 : 0;
}
