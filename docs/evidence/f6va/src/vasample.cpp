/* f6va: one sample of a process's address-space use, on one line.
 *
 *   vasample.exe <pid|image-name> [--dump]
 *
 * Prints
 *   va_used_mib=<committed + reserved, below 4 GiB> free_mib=<free below
 *   4 GiB> largest_free_mib=<largest free block> regions_4m=<committed
 *   private regions of exactly 4 MiB> mib_4m=<their total>
 *   private_mib=<PrivateUsage> ws_mib=<WorkingSetSize>
 *   protect_0x<page protection>=<4 MiB regions with it> ...
 * or "none" when no such process runs. --dump first prints the base address
 * and first 32 bytes of up to four plain read-write 4 MiB regions. VirtualQueryEx walks the address
 * space; GetProcessMemoryInfo gives the commit charge and the working set.
 * Kernel-mode mappings into the process (the KMD's blob maps) show as
 * private regions with no commit charge, so va_used_mib, not private_mib, is
 * what runs out in a 32-bit process. tests: docs/evidence/f6va/src/sample-va.sh.
 */
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <map>

static DWORD find_pid(const wchar_t *name)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e = { sizeof e };
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(s, &e); ok; ok = Process32NextW(s, &e))
        if (!_wcsicmp(e.szExeFile, name)) { pid = e.th32ProcessID; break; }
    CloseHandle(s);
    return pid;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) { fwprintf(stderr, L"usage: vasample <pid|name>\n"); return 2; }
    DWORD pid = wcstoul(argv[1], NULL, 10);
    if (!pid) pid = find_pid(argv[1]);
    HANDLE h = pid ? OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid) : NULL;
    if (!h) { printf("none\n"); return 0; }
    const unsigned long long limit = 0x100000000ull;
    unsigned long long used = 0, free_total = 0, free_largest = 0, mib4 = 0;
    unsigned regions4 = 0, dumped = 0;
    const bool dump = argc > 2 && !wcscmp(argv[2], L"--dump");
    std::map<DWORD, unsigned> protect4;
    MEMORY_BASIC_INFORMATION m;
    for (unsigned long long a = 0; a < limit && VirtualQueryEx(h, (void *)a, &m, sizeof m);
         a = (unsigned long long)m.BaseAddress + m.RegionSize) {
        unsigned long long base = (unsigned long long)m.BaseAddress;
        unsigned long long size = m.RegionSize;
        if (base + size > limit) size = limit - base;
        if (m.State == MEM_FREE) {
            free_total += size;
            if (size > free_largest) free_largest = size;
            continue;
        }
        used += size;
        if (m.Type == MEM_PRIVATE && m.State == MEM_COMMIT && m.RegionSize == 0x400000) {
            regions4++;
            mib4 += m.RegionSize;
            protect4[m.Protect]++;
            if (dump && m.Protect == PAGE_READWRITE && dumped < 4) {
                unsigned char b[32] = {};
                SIZE_T got = 0;
                ReadProcessMemory(h, m.BaseAddress, b, sizeof b, &got);
                printf("region base=%p alloc_base=%p bytes=", m.BaseAddress, m.AllocationBase);
                for (SIZE_T i = 0; i < got; i++) printf("%02x", b[i]);
                printf("\n");
                dumped++;
            }
        }
    }
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof pmc;
    GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc);
    printf("va_used_mib=%.0f free_mib=%.0f largest_free_mib=%.1f regions_4m=%u mib_4m=%.0f private_mib=%.0f ws_mib=%.0f",
           used / 1048576.0, free_total / 1048576.0, free_largest / 1048576.0,
           regions4, mib4 / 1048576.0, pmc.PrivateUsage / 1048576.0,
           pmc.WorkingSetSize / 1048576.0);
    for (auto &kv : protect4)
        printf(" protect_0x%lx=%u", kv.first, kv.second);
    printf("\n");
    CloseHandle(h);
    return 0;
}
