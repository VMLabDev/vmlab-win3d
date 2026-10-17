/* Loads the d3d12.dll bridge next to this executable, with fake_native.dll
 * as vmlab-win3d\d3d12-native.dll and no vkd3dcore.dll, and checks that:
 * forwarded exports reach the native DLL with their arguments intact
 * (integer, pointer and float arguments; 64-bit returns), missing native
 * exports return their fallback with the stack balanced, the ordinal-only
 * export forwards by ordinal, and without a core the vkd3d-proton exports
 * fall back to the native DLL. Prints PASS/FAIL lines; exit code = failures. */
#include <windows.h>
#include <stdio.h>

static int failures;
#define CHECK(cond, ...) do { printf("%s: ", (cond) ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); \
    if (!(cond)) failures++; } while (0)

typedef UINT_PTR (WINAPI *ord99_fn)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
typedef HRESULT (WINAPI *compat_fn)(SIZE_T, const char *);
typedef SIZE_T (WINAPI *size_fn)(const void *, UINT);
typedef void *(WINAPI *thread_fn)(void);
typedef void (WINAPI *counter_fn)(const WCHAR *, float);
typedef UINT64 (WINAPI *replace_fn)(BOOL);
typedef HRESULT (WINAPI *two_fn)(const void *, UINT);
typedef HRESULT (WINAPI *create_fn)(IUnknown *, UINT, REFIID, void **);

#ifdef _M_IX86
/* Call through a stdcall pointer and report whether esp came back intact. */
static volatile UINT_PTR esp_before, esp_after;
#define SAVE_ESP() __asm__ volatile("movl %%esp, %0" : "=m"(esp_before))
#define LOAD_ESP() __asm__ volatile("movl %%esp, %0" : "=m"(esp_after))
#else
static volatile UINT_PTR esp_before, esp_after;
#define SAVE_ESP() (void)0
#define LOAD_ESP() (void)0
#endif

int main(void)
{
    HMODULE bridge, native;
    float *last_counter;
    const WCHAR **last_name;
    void *device = (void *)1;
    HRESULT hr;

    if (!(bridge = LoadLibraryA("d3d12.dll")))
    {
        printf("FAIL: cannot load d3d12.dll, error %lu\n", GetLastError());
        return 1;
    }

    {
        ord99_fn f = (ord99_fn)(void *)GetProcAddress(bridge, MAKEINTRESOURCEA(99));
        CHECK(f && f(1, 2, 3, 4) == 0x9900 + 10, "ordinal 99 forwards by ordinal with four arguments");
    }
    {
        compat_fn f = (compat_fn)(void *)GetProcAddress(bridge, "SetAppCompatStringPointer");
        CHECK(f && f(5, "compat") == 0x5e7, "SetAppCompatStringPointer forwards pointer arguments");
    }
    {
        size_fn f = (size_fn)(void *)GetProcAddress(bridge, "D3D12CoreGetLayeredDeviceSize");
        CHECK(f && f(&device, 7) == 0x1234 + 7 + 1, "D3D12CoreGetLayeredDeviceSize forwards and returns SIZE_T");
        CHECK(f && f(NULL, 1) == 0x1235, "second call goes through the resolved pointer");
    }
    {
        thread_fn f = (thread_fn)(void *)GetProcAddress(bridge, "D3D12PIXGetThreadInfo");
        native = GetModuleHandleA("d3d12-native.dll");
        CHECK(f && native && f() != NULL, "D3D12PIXGetThreadInfo forwards with no arguments");
    }
    {
        counter_fn f = (counter_fn)(void *)GetProcAddress(bridge, "D3D12PIXReportCounter");
        last_counter = native ? (float *)(void *)GetProcAddress(native, "last_counter") : NULL;
        last_name = native ? (const WCHAR **)(void *)GetProcAddress(native, "last_counter_name") : NULL;
        if (f)
            f(L"frames", 2.5f);
        CHECK(f && last_counter && *last_counter == 2.5f && last_name && !lstrcmpW(*last_name, L"frames"),
                "D3D12PIXReportCounter forwards a float argument");
    }
    {
        replace_fn f = (replace_fn)(void *)GetProcAddress(bridge, "D3D12PIXEventsReplaceBlock");
        CHECK(f && f(TRUE) == 0x1122334455667788ull, "D3D12PIXEventsReplaceBlock returns a 64-bit value");
    }
    {
        two_fn f = (two_fn)(void *)GetProcAddress(bridge, "GetBehaviorValue");
        UINT_PTR r;
        SAVE_ESP();
        r = f ? (UINT_PTR)f("x", 1) : 1;
        LOAD_ESP();
        CHECK(f && r == 0 && esp_before == esp_after, "missing GetBehaviorValue returns 0, stack balanced");
    }
    {
        two_fn f = (two_fn)(void *)GetProcAddress(bridge, "D3D12CoreRegisterLayers");
        HRESULT r;
        SAVE_ESP();
        r = f ? f(NULL, 0) : S_OK;
        LOAD_ESP();
        CHECK(f && r == E_NOTIMPL && esp_before == esp_after, "missing D3D12CoreRegisterLayers returns E_NOTIMPL, stack balanced");
    }
    CHECK(GetProcAddress(bridge, "D3D12DeviceRemovedExtendedData") != NULL, "D3D12DeviceRemovedExtendedData data export present");
    {
        static const GUID iid = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
        create_fn f = (create_fn)(void *)GetProcAddress(bridge, "D3D12CreateDevice");
        hr = f ? f(NULL, 0xb000, &iid, &device) : E_FAIL;
        CHECK(hr == 0x0d3d1200 && device == NULL, "without vkd3dcore.dll, D3D12CreateDevice falls back to native");
    }

    printf("%d failure(s)\n", failures);
    return failures;
}
