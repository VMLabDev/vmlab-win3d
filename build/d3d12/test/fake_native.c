/* Stand-in for the preserved native d3d12.dll (vmlab-win3d\d3d12-native.dll)
 * in the bridge forwarding test. It deliberately lacks GetBehaviorValue and
 * D3D12CoreRegisterLayers so the test can check the fallbacks. */
#include <windows.h>

static char thread_info[16];
float last_counter;
const WCHAR *last_counter_name;

UINT_PTR WINAPI native_ordinal_99(UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d)
{
    return 0x9900 + a + b + c + d;
}

HRESULT WINAPI SetAppCompatStringPointer(SIZE_T size, const char *string)
{
    return size == 5 && !lstrcmpA(string, "compat") ? 0x5e7 : E_FAIL;
}

SIZE_T WINAPI D3D12CoreGetLayeredDeviceSize(const void *data, UINT size)
{
    return 0x1234 + size + (data ? 1 : 0);
}

void *WINAPI D3D12PIXGetThreadInfo(void)
{
    return thread_info;
}

void WINAPI D3D12PIXReportCounter(const WCHAR *name, float value)
{
    last_counter_name = name;
    last_counter = value;
}

UINT64 WINAPI D3D12PIXEventsReplaceBlock(BOOL earliest)
{
    return earliest ? 0x1122334455667788ull : 1;
}

HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter, UINT level, REFIID iid, void **device)
{
    *device = NULL;
    return 0x0d3d1200 + (level & 0xff);
}
