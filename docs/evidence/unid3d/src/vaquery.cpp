/* unid3d F6: where a process's virtual address space goes.
 *
 *   vaquery.exe <pid|image-name>
 *
 * Walks the process's address space with VirtualQueryEx and prints, per
 * region type (image, mapped, private) and state (committed, reserved), the
 * number of regions and their total size, then the 20 most common committed
 * private region sizes. Kernel-mode mappings into the process (the KMD's
 * blob maps) show as private regions with no commit charge.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <map>
#include <vector>
#include <algorithm>

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
    if (argc < 2) { fwprintf(stderr, L"usage: vaquery <pid|name>\n"); return 2; }
    DWORD pid = wcstoul(argv[1], NULL, 10);
    if (!pid) pid = find_pid(argv[1]);
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) { fwprintf(stderr, L"OpenProcess %lu failed %lu\n", pid, GetLastError()); return 1; }
    std::map<std::pair<DWORD, DWORD>, std::pair<unsigned, unsigned long long>> sums;
    std::map<unsigned long long, unsigned> priv_sizes;
    unsigned long long free_total = 0, free_largest = 0;
    MEMORY_BASIC_INFORMATION m;
    for (unsigned long long a = 0; VirtualQueryEx(h, (void *)a, &m, sizeof m); a = (unsigned long long)m.BaseAddress + m.RegionSize) {
        if (m.State == MEM_FREE) {
            if ((unsigned long long)m.BaseAddress < 0x100000000ull) {
                free_total += m.RegionSize;
                free_largest = std::max<unsigned long long>(free_largest, m.RegionSize);
            }
            if ((unsigned long long)m.BaseAddress >= 0x100000000ull) break;
            continue;
        }
        auto &s = sums[{m.Type, m.State}];
        s.first++;
        s.second += m.RegionSize;
        if (m.Type == MEM_PRIVATE && m.State == MEM_COMMIT) priv_sizes[m.RegionSize]++;
    }
    printf("pid %lu\n", pid);
    for (auto &kv : sums) {
        const char *t = kv.first.first == MEM_IMAGE ? "image" : kv.first.first == MEM_MAPPED ? "mapped" : "private";
        const char *st = kv.first.second == MEM_COMMIT ? "commit" : "reserve";
        printf("%-8s %-8s regions=%6u MiB=%8.1f\n", t, st, kv.second.first, kv.second.second / 1048576.0);
    }
    printf("free below 4 GiB MiB=%.1f largest=%.1f\n", free_total / 1048576.0, free_largest / 1048576.0);
    std::vector<std::pair<unsigned long long, unsigned>> v(priv_sizes.begin(), priv_sizes.end());
    std::sort(v.begin(), v.end(), [](auto &x, auto &y) { return x.first * x.second > y.first * y.second; });
    printf("committed private regions by total size:\n");
    for (size_t i = 0; i < v.size() && i < 20; i++)
        printf("  size=0x%llx count=%u MiB=%.1f\n", v[i].first, v[i].second, v[i].first * v[i].second / 1048576.0);
    CloseHandle(h);
    return 0;
}
