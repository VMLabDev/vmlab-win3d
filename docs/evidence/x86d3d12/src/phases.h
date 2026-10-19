/* Phase tracer and stall watchdog for the x86d3d12 repro.
 *
 * ph_open(path) starts a trace file; PH("name") writes "<ms since process
 * start> <name>" and flushes, so a stalled or killed process still leaves its
 * last phase on disk. A watchdog thread checks the main thread's current
 * phase: when one phase has lasted ph_stall_ms, it suspends every other
 * thread of the process, scans each one's stack for return addresses inside
 * loaded modules, and writes them as module+offset (with the nearest
 * preceding export where the module has one). It does this again at 3x and
 * 9x ph_stall_ms. The result says which DLL each thread is waiting in. */
#ifndef PHASES_H
#define PHASES_H

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FILE *ph_file;
static LARGE_INTEGER ph_freq, ph_t0;
static volatile LONG ph_seq;           /* bumps on every PH() */
static volatile const char *ph_name;   /* current phase */
static volatile LONGLONG ph_since;     /* QPC at the current phase's start */
static DWORD ph_main_tid;
static DWORD ph_stall_ms = 5000;
static CRITICAL_SECTION ph_lock;

static double ph_ms(LONGLONG qpc) { return 1000.0 * (double)(qpc - ph_t0.QuadPart) / (double)ph_freq.QuadPart; }

static void ph_printf(const char *fmt, ...)
{
    if (!ph_file)
        return;
    EnterCriticalSection(&ph_lock);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(ph_file, fmt, ap);
    va_end(ap);
    fflush(ph_file);
    LeaveCriticalSection(&ph_lock);
}

static void PH(const char *name)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    ph_name = name;
    ph_since = now.QuadPart;
    InterlockedIncrement(&ph_seq);
    ph_printf("%10.1f phase %s\n", ph_ms(now.QuadPart), name);
}

/* Nearest export at or below rva in the module at base, or NULL. */
static const char *ph_export(const BYTE *base, DWORD rva, DWORD *off)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd->VirtualAddress || !dd->Size)
        return NULL;
    const IMAGE_EXPORT_DIRECTORY *ed = (const IMAGE_EXPORT_DIRECTORY *)(base + dd->VirtualAddress);
    const DWORD *funcs = (const DWORD *)(base + ed->AddressOfFunctions);
    const DWORD *names = (const DWORD *)(base + ed->AddressOfNames);
    const WORD *ords = (const WORD *)(base + ed->AddressOfNameOrdinals);
    const char *best = NULL;
    DWORD best_rva = 0;
    for (DWORD i = 0; i < ed->NumberOfNames; i++) {
        DWORD f = funcs[ords[i]];
        if (f <= rva && f >= best_rva && !(f >= dd->VirtualAddress && f < dd->VirtualAddress + dd->Size)) {
            best_rva = f;
            best = (const char *)(base + names[i]);
        }
    }
    if (best)
        *off = rva - best_rva;
    return best;
}

typedef struct { const BYTE *base; DWORD size; char name[64]; } ph_mod;

static int ph_modules(ph_mod *mods, int max)
{
    HMODULE hm[512];
    DWORD need = 0;
    if (!EnumProcessModules(GetCurrentProcess(), hm, sizeof hm, &need))
        return 0;
    int n = (int)(need / sizeof(HMODULE));
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++) {
        MODULEINFO mi = {};
        GetModuleInformation(GetCurrentProcess(), hm[i], &mi, sizeof mi);
        mods[i].base = (const BYTE *)mi.lpBaseOfDll;
        mods[i].size = mi.SizeOfImage;
        if (!GetModuleBaseNameA(GetCurrentProcess(), hm[i], mods[i].name, sizeof mods[i].name))
            strcpy(mods[i].name, "?");
    }
    return n;
}

static void ph_sym(const ph_mod *mods, int n, ULONG_PTR a, char *out, size_t size)
{
    for (int i = 0; i < n; i++) {
        if (a >= (ULONG_PTR)mods[i].base && a < (ULONG_PTR)mods[i].base + mods[i].size) {
            DWORD rva = (DWORD)(a - (ULONG_PTR)mods[i].base), off = 0;
            const char *e = ph_export(mods[i].base, rva, &off);
            if (e)
                snprintf(out, size, "%s+0x%lx (%s+0x%lx)", mods[i].name, (unsigned long)rva, e, (unsigned long)off);
            else
                snprintf(out, size, "%s+0x%lx", mods[i].name, (unsigned long)rva);
            return;
        }
    }
    out[0] = 0;
}

/* Whether a points just after a call instruction (a plausible return address). */
static int ph_after_call(ULONG_PTR a)
{
    const BYTE *p = (const BYTE *)a;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p - 7, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return 0;
    if (p[-5] == 0xe8)                       /* call rel32 */
        return 1;
    if (p[-2] == 0xff && (p[-1] & 0x38) == 0x10) /* call r/m, mod 11 */
        return 1;
    if (p[-3] == 0xff && (p[-2] & 0x38) == 0x10) /* call [r+disp8] */
        return 1;
    if (p[-6] == 0xff && (p[-5] & 0x38) == 0x10) /* call [disp32] / [r+disp32] */
        return 1;
    if (p[-7] == 0xff && (p[-6] & 0x38) == 0x10) /* call [sib+disp32] */
        return 1;
    return 0;
}

static void ph_dump_threads(const char *why)
{
    static ph_mod mods[512];
    int nmod = ph_modules(mods, 512);
    ph_printf("---- stack scan (%s), phase '%s' ----\n", why, (const char *)ph_name);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    THREADENTRY32 te = {sizeof te};
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
            continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                               te.th32ThreadID);
        if (!th)
            continue;
        /* Copy the stack while the thread is suspended, then resume it before
         * doing anything that can take the loader lock or allocate. */
        static ULONG_PTR stack[8192];
        SIZE_T got = 0;
        ULONG_PTR ip = 0, sp = 0;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(th, &c)) {
#ifdef _WIN64
                ip = c.Rip; sp = c.Rsp;
#else
                ip = c.Eip; sp = c.Esp;
#endif
                ReadProcessMemory(GetCurrentProcess(), (void *)sp, stack, sizeof stack, &got);
                if (!got) {
                    /* The read stops at the stack's top; retry page by page. */
                    SIZE_T part = 0;
                    for (SIZE_T o = 0; o < sizeof stack; o += 4096) {
                        if (!ReadProcessMemory(GetCurrentProcess(), (BYTE *)sp + o, (BYTE *)stack + o, 4096, &part))
                            break;
                        got = o + part;
                    }
                }
            }
            ResumeThread(th);
        }
        CloseHandle(th);
        char sym[256];
        ph_sym(mods, nmod, ip, sym, sizeof sym);
        ph_printf("thread %lu%s ip %s\n", (unsigned long)te.th32ThreadID,
                  te.th32ThreadID == ph_main_tid ? " (main)" : "", sym[0] ? sym : "?");
        int shown = 0;
        for (SIZE_T i = 0; i < got / sizeof(ULONG_PTR) && shown < 40; i++) {
            ULONG_PTR a = stack[i];
            ph_sym(mods, nmod, a, sym, sizeof sym);
            if (sym[0] && ph_after_call(a)) {
                ph_printf("    sp+0x%04lx %s\n", (unsigned long)(i * sizeof(ULONG_PTR)), sym);
                shown++;
            }
        }
    }
    CloseHandle(snap);
    ph_printf("---- end stack scan ----\n");
}

static DWORD WINAPI ph_watchdog(void *arg)
{
    LONG seen = -1;
    int dumps = 0;
    for (;;) {
        Sleep(250);
        LONG seq = ph_seq;
        if (seq != seen) {
            seen = seq;
            dumps = 0;
            continue;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double held = 1000.0 * (double)(now.QuadPart - ph_since) / (double)ph_freq.QuadPart;
        static const int mult[] = {1, 3, 9};
        if (dumps < 3 && held >= ph_stall_ms * mult[dumps]) {
            char why[64];
            snprintf(why, sizeof why, "phase held %.0f ms", held);
            ph_printf("%10.1f STALL %s in '%s'\n", ph_ms(now.QuadPart), why, (const char *)ph_name);
            ph_dump_threads(why);
            dumps++;
        }
    }
    return 0;
}

/* Opens the trace at path (UTF-16) and starts the watchdog. */
static void ph_open(const wchar_t *path)
{
    InitializeCriticalSection(&ph_lock);
    QueryPerformanceFrequency(&ph_freq);
    /* Measure from process creation so loader time before main shows. */
    FILETIME ct, et, kt, ut, nowft;
    GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut);
    GetSystemTimePreciseAsFileTime(&nowft);
    QueryPerformanceCounter(&ph_t0);
    ULONGLONG since_create = (((ULONGLONG)nowft.dwHighDateTime << 32) | nowft.dwLowDateTime) -
                             (((ULONGLONG)ct.dwHighDateTime << 32) | ct.dwLowDateTime);
    ph_t0.QuadPart -= (LONGLONG)(since_create * (ULONGLONG)ph_freq.QuadPart / 10000000ull);
    ph_file = _wfopen(path, L"w");
    ph_main_tid = GetCurrentThreadId();
    wchar_t v[16];
    if (GetEnvironmentVariableW(L"PH_STALL_MS", v, 16))
        ph_stall_ms = (DWORD)_wtoi(v);
    SYSTEMTIME st;
    GetSystemTime(&st);
    ph_printf("# x86d3d12 phase trace, pid %lu, %04d-%02d-%02dT%02d:%02d:%02d.%03dZ, stall %lu ms\n",
              (unsigned long)GetCurrentProcessId(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
              st.wSecond, st.wMilliseconds, (unsigned long)ph_stall_ms);
    PH("main");
    CloseHandle(CreateThread(NULL, 0, ph_watchdog, NULL, 0, NULL));
}

#endif
