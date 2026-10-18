/*
 * modeprobe: every display-mode list a program can see, side by side.
 *
 *   modeprobe.exe [--set] [--out FILE]
 *
 * It prints one JSON object with, per source, the number of modes at each
 * colour depth (or format) and whether 640x480 at 16 bpp is among them:
 *   - D3DKMTGetDisplayModeList on the primary's adapter and VidPN source
 *     (the kernel's list, with each mode's D3DDDI format and flags);
 *   - EnumDisplaySettingsEx, plain and with EDS_RAWMODE;
 *   - ChangeDisplaySettingsEx(640x480x16, CDS_TEST);
 *   - IDirect3D9::GetAdapterModeCount and CheckDeviceType for R5G6B5 and
 *     X8R8G8B8;
 *   - IDirectDraw7::EnumDisplayModes, and with --set,
 *     SetDisplayMode(640, 480, 16) under DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN,
 *     then RestoreDisplayMode.
 */
#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <ddraw.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char g_json[262144];
static size_t g_len;

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_json + g_len, sizeof(g_json) - g_len, fmt, ap);
    va_end(ap);
    if (n > 0 && g_len + (size_t)n < sizeof(g_json))
        g_len += (size_t)n;
}

/* d3dkmthk.h is not in the toolchain; these are its Windows 10 layouts.
 * D3DKMT_DISPLAYMODE is 44 bytes: d3d9.dll walks the list with that stride. */
typedef UINT D3DKMT_HANDLE;
typedef struct {
    WCHAR DeviceName[32];
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;
    UINT VidPnSourceId;
} KMT_OPENADAPTERFROMGDIDISPLAYNAME;
typedef struct {
    UINT Width, Height;
    UINT Format; /* D3DDDIFORMAT, the same values as D3DFORMAT */
    UINT IntegerRefreshRate;
    UINT RefreshNum, RefreshDen;
    UINT ScanLineOrdering, DisplayOrientation, DisplayFixedOutput;
    UINT Flags, Flags2;
} KMT_DISPLAYMODE;
typedef struct {
    D3DKMT_HANDLE hAdapter;
    UINT VidPnSourceId;
    KMT_DISPLAYMODE *pModeList;
    UINT ModeCount;
} KMT_GETDISPLAYMODELIST;
typedef struct {
    D3DKMT_HANDLE hAdapter;
} KMT_CLOSEADAPTER;
typedef LONG(APIENTRY *PFN_OPEN)(KMT_OPENADAPTERFROMGDIDISPLAYNAME *);
typedef LONG(APIENTRY *PFN_LIST)(KMT_GETDISPLAYMODELIST *);
typedef LONG(APIENTRY *PFN_CLOSE)(KMT_CLOSEADAPTER *);

static void kmt_modes(void)
{
    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    PFN_OPEN open = (PFN_OPEN)(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromGdiDisplayName");
    PFN_LIST list = (PFN_LIST)(void *)GetProcAddress(gdi, "D3DKMTGetDisplayModeList");
    PFN_CLOSE close = (PFN_CLOSE)(void *)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    out("\"kmt\": {");
    if (!open || !list) {
        out("\"error\": \"no D3DKMT exports\"}");
        return;
    }
    KMT_OPENADAPTERFROMGDIDISPLAYNAME oa = {0};
    wcscpy(oa.DeviceName, L"\\\\.\\DISPLAY1");
    LONG st = open(&oa);
    out("\"open_status\": \"0x%08lx\", \"source\": %u", (unsigned long)st, oa.VidPnSourceId);
    if (st) {
        out("}");
        return;
    }
    KMT_GETDISPLAYMODELIST ml = {oa.hAdapter, oa.VidPnSourceId, NULL, 0};
    st = list(&ml);
    static KMT_DISPLAYMODE modes[4096];
    if (!st && ml.ModeCount <= 4096) {
        ml.pModeList = modes;
        st = list(&ml);
    }
    out(", \"list_status\": \"0x%08lx\", \"count\": %u, \"modes\": [", (unsigned long)st, ml.ModeCount);
    for (UINT i = 0; !st && i < ml.ModeCount; i++)
        out("%s\"%ux%u fmt=%u %uHz flags=0x%x,0x%x\"", i ? ", " : "", modes[i].Width,
            modes[i].Height, modes[i].Format, modes[i].IntegerRefreshRate, modes[i].Flags,
            modes[i].Flags2);
    out("]}");
    KMT_CLOSEADAPTER ca = {oa.hAdapter};
    if (close)
        close(&ca);
}

static void eds(const char *name, DWORD flags)
{
    int by_bpp[33] = {0}, have = 0, n = 0;
    DEVMODEA dm;
    for (DWORD i = 0;; i++) {
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsExA(NULL, i, &dm, flags))
            break;
        n++;
        if (dm.dmBitsPerPel <= 32)
            by_bpp[dm.dmBitsPerPel]++;
        have |= dm.dmPelsWidth == 640 && dm.dmPelsHeight == 480 && dm.dmBitsPerPel == 16;
    }
    out("\"%s\": {\"count\": %d, \"bpp8\": %d, \"bpp16\": %d, \"bpp32\": %d, \"has_640x480x16\": %s}",
        name, n, by_bpp[8], by_bpp[16], by_bpp[32], have ? "true" : "false");
}

static void cds_test(void)
{
    DEVMODEA dm = {0};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = 640;
    dm.dmPelsHeight = 480;
    dm.dmBitsPerPel = 16;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    LONG r = ChangeDisplaySettingsExA(NULL, &dm, NULL, CDS_TEST, NULL);
    out("\"cds_test_640x480x16\": %ld", r);
}

static void d3d9_modes(void)
{
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    out("\"d3d9\": {");
    if (!d3d) {
        out("\"error\": \"Direct3DCreate9 failed\"}");
        return;
    }
    D3DADAPTER_IDENTIFIER9 id;
    if (SUCCEEDED(IDirect3D9_GetAdapterIdentifier(d3d, 0, 0, &id)))
        out("\"adapter\": \"%s\", \"driver\": \"%s\", ", id.Description, id.Driver);
    static const struct { D3DFORMAT f; const char *n; } fmts[] = {
        {D3DFMT_R5G6B5, "R5G6B5"}, {D3DFMT_X1R5G5B5, "X1R5G5B5"}, {D3DFMT_X8R8G8B8, "X8R8G8B8"},
    };
    for (unsigned i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        UINT n = IDirect3D9_GetAdapterModeCount(d3d, 0, fmts[i].f);
        HRESULT ct = IDirect3D9_CheckDeviceType(d3d, 0, D3DDEVTYPE_HAL, fmts[i].f, fmts[i].f, FALSE);
        int have = 0;
        for (UINT m = 0; m < n; m++) {
            D3DDISPLAYMODE dm;
            if (SUCCEEDED(IDirect3D9_EnumAdapterModes(d3d, 0, fmts[i].f, m, &dm)))
                have |= dm.Width == 640 && dm.Height == 480;
        }
        out("%s\"%s\": {\"modes\": %u, \"has_640x480\": %s, \"check_device_type_fullscreen\": \"0x%08lx\"}",
            i ? ", " : "", fmts[i].n, n, have ? "true" : "false", (unsigned long)ct);
    }
    out("}");
    IDirect3D9_Release(d3d);
}

static int g_dd_bpp[33], g_dd_n, g_dd_have;
static HRESULT WINAPI enum_mode(LPDDSURFACEDESC2 sd, LPVOID ctx)
{
    DWORD b = sd->ddpfPixelFormat.dwRGBBitCount;
    g_dd_n++;
    if (b <= 32)
        g_dd_bpp[b]++;
    g_dd_have |= sd->dwWidth == 640 && sd->dwHeight == 480 && b == 16;
    return DDENUMRET_OK;
}

static void ddraw_modes(int set)
{
    IDirectDraw7 *dd = NULL;
    HRESULT hr = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    out("\"ddraw\": {\"create_hr\": \"0x%08lx\"", (unsigned long)hr);
    if (FAILED(hr)) {
        out("}");
        return;
    }
    hr = IDirectDraw7_EnumDisplayModes(dd, 0, NULL, NULL, enum_mode);
    out(", \"enum_hr\": \"0x%08lx\", \"count\": %d, \"bpp8\": %d, \"bpp16\": %d, \"bpp32\": %d, "
        "\"has_640x480x16\": %s",
        (unsigned long)hr, g_dd_n, g_dd_bpp[8], g_dd_bpp[16], g_dd_bpp[32], g_dd_have ? "true" : "false");
    if (set) {
        HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, "STATIC", "modeprobe", WS_POPUP | WS_VISIBLE, 0, 0,
                                    640, 480, NULL, NULL, GetModuleHandleA(NULL), NULL);
        hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
        out(", \"coop_hr\": \"0x%08lx\"", (unsigned long)hr);
        hr = IDirectDraw7_SetDisplayMode(dd, 640, 480, 16, 0, 0);
        out(", \"set_640x480x16_hr\": \"0x%08lx\"", (unsigned long)hr);
        if (SUCCEEDED(hr)) {
            DDSURFACEDESC2 m = {0};
            m.dwSize = sizeof(m);
            IDirectDraw7_GetDisplayMode(dd, &m);
            DEVMODEA dm = {0};
            dm.dmSize = sizeof(dm);
            EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm);
            out(", \"ddraw_mode\": \"%lux%lux%lu\", \"os_mode\": \"%lux%lux%lu\"",
                (unsigned long)m.dwWidth, (unsigned long)m.dwHeight,
                (unsigned long)m.ddpfPixelFormat.dwRGBBitCount, (unsigned long)dm.dmPelsWidth,
                (unsigned long)dm.dmPelsHeight, (unsigned long)dm.dmBitsPerPel);
            IDirectDraw7_RestoreDisplayMode(dd);
        }
        IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
        DestroyWindow(hwnd);
    }
    out("}");
    IDirectDraw7_Release(dd);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int set = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--set"))
            set = 1;
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            path = argv[++i];
    }
    out("{\"arch\": \"%s\", ", sizeof(void *) == 8 ? "x64" : "x86");
    DEVMODEA cur = {0};
    cur.dmSize = sizeof(cur);
    EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &cur);
    out("\"desktop\": \"%lux%lux%lu\", ", (unsigned long)cur.dmPelsWidth,
        (unsigned long)cur.dmPelsHeight, (unsigned long)cur.dmBitsPerPel);
    eds("eds", 0);
    out(", ");
    eds("eds_raw", EDS_RAWMODE);
    out(", ");
    cds_test();
    out(", ");
    d3d9_modes();
    out(", ");
    ddraw_modes(set);
    out(", ");
    kmt_modes();
    out("}\n");
    fputs(g_json, stdout);
    if (path) {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs(g_json, f);
            fclose(f);
        }
    }
    return 0;
}
