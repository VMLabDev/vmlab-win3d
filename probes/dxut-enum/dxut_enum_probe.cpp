/* DXUT enumeration probe: replays the D3D11 calls that the DirectX SDK (June
 * 2010) DXUT framework makes before its sample window appears
 * (DXUT11/Core/DXUTDevice11.cpp CD3D11Enumeration::Enumerate, DXUT.cpp
 * DXUTCreateD3D11Views), and prints every HRESULT with its duration, one line
 * at a time, so the last line names the call that hung.
 *
 *   dxut_enum_probe.exe [--warp] [--log <file>] [--frames <n>]
 *
 * --warp runs every device on D3D_DRIVER_TYPE_WARP instead of HARDWARE, as a
 * reference on the same VM. Exit 0 when every step returned, 1 when a step
 * failed that DXUT treats as fatal. Built by probes/dxut-enum/build.sh. */
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *g_log;
static LARGE_INTEGER g_freq, g_t0;
static D3D_DRIVER_TYPE g_type = D3D_DRIVER_TYPE_HARDWARE;
static int g_fatal;

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

static void say(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%10.1f %s\n", now_ms(), buf);
    fflush(stdout);
    if (g_log) {
        fprintf(g_log, "%10.1f %s\n", now_ms(), buf);
        fflush(g_log);
    }
}

/* Logs the call before it runs and its result after, so a hang leaves the
 * "call" line as the last one. */
#define STEP(hrvar, desc, expr)                                                 \
    do {                                                                        \
        say("call %s", desc);                                                   \
        double t_ = now_ms();                                                   \
        hrvar = (expr);                                                         \
        say("  -> 0x%08lx (%.1f ms) %s", (unsigned long)(hrvar), now_ms() - t_, \
            desc);                                                              \
    } while (0)

static const char *fmt_name(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32_FLOAT_S8X24_UINT";
    case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
    case DXGI_FORMAT_D16_UNORM: return "D16_UNORM";
    default: return "other";
    }
}

static const DXGI_FORMAT bb_formats[] = {
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM};
static const DXGI_FORMAT ds_formats[] = {
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_D32_FLOAT,
    DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D16_UNORM};
static const D3D_FEATURE_LEVEL all_levels[] = {
    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1};

static HRESULT create(D3D_DRIVER_TYPE type, const D3D_FEATURE_LEVEL *levels, UINT n,
                      ID3D11Device **dev, D3D_FEATURE_LEVEL *fl, ID3D11DeviceContext **dc,
                      const char *what)
{
    char desc[160];
    HRESULT hr;
    snprintf(desc, sizeof(desc), "D3D11CreateDevice(%s, %u levels) [%s]",
             type == D3D_DRIVER_TYPE_WARP ? "WARP"
             : type == D3D_DRIVER_TYPE_REFERENCE ? "REFERENCE" : "HARDWARE",
             n, what);
    STEP(hr, desc, D3D11CreateDevice(NULL, type, NULL, 0, levels, n, D3D11_SDK_VERSION, dev, fl, dc));
    if (SUCCEEDED(hr))
        say("  feature level 0x%x", (unsigned)*fl);
    return hr;
}

static void release_dev(ID3D11Device *dev, ID3D11DeviceContext *dc, const char *what)
{
    ULONG rc_dc = dc ? dc->Release() : 0;
    ULONG rc = dev ? dev->Release() : 0;
    say("  release [%s]: context ref %lu, device ref %lu%s", what, rc_dc, rc,
        rc ? "  <-- device still referenced" : "");
}

/* CD3D11Enumeration::EnumerateDisplayModes, all formats. */
static int enum_modes(IDXGIOutput *out)
{
    int total = 0;
    for (DXGI_FORMAT f : bb_formats) {
        UINT n = 512;
        static DXGI_MODE_DESC modes[512];
        char desc[128];
        HRESULT hr;
        snprintf(desc, sizeof(desc), "IDXGIOutput::GetDisplayModeList(%s, SCALING, 512)", fmt_name(f));
        STEP(hr, desc, out->GetDisplayModeList(f, DXGI_ENUM_MODES_SCALING, &n, modes));
        if (SUCCEEDED(hr)) {
            say("  %u modes", n);
            for (UINT i = 0; i < n && i < 4; i++)
                say("    %ux%u %u/%u scanline %d scaling %d", modes[i].Width, modes[i].Height,
                    modes[i].RefreshRate.Numerator, modes[i].RefreshRate.Denominator,
                    modes[i].ScanlineOrdering, modes[i].Scaling);
            total += (int)n;
        }
    }
    return total;
}

static void enum_devices(D3D_FEATURE_LEVEL *max_fl)
{
    /* EnumerateDevices: HARDWARE, WARP, REFERENCE with all six levels, then
     * QI IDXGIDevice1 / GetAdapter and the D3D10_X hardware options. */
    const D3D_DRIVER_TYPE types[] = {g_type, D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_REFERENCE};
    for (int t = 0; t < 3; t++) {
        if (t == 1 && g_type == D3D_DRIVER_TYPE_WARP)
            continue;
        ID3D11Device *dev = NULL;
        ID3D11DeviceContext *dc = NULL;
        D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
        if (FAILED(create(types[t], all_levels, ARRAYSIZE(all_levels), &dev, &fl, &dc, "EnumerateDevices")))
            continue;
        if (t == 0)
            *max_fl = fl;
        IDXGIDevice1 *dxgi = NULL;
        IDXGIAdapter *ad = NULL;
        HRESULT hr;
        STEP(hr, "ID3D11Device::QueryInterface(IDXGIDevice1)", dev->QueryInterface(__uuidof(IDXGIDevice1), (void **)&dxgi));
        if (SUCCEEDED(hr)) {
            STEP(hr, "IDXGIDevice1::GetAdapter", dxgi->GetAdapter(&ad));
            /* DXUT replaces the adapter it enumerates with this one, once
             * per device type, and then enumerates the outputs of the last. */
            if (ad) {
                DXGI_ADAPTER_DESC d = {};
                ad->GetDesc(&d);
                say("  device adapter: %ls vendor 0x%04x device 0x%04x luid %08lx:%08lx", d.Description,
                    d.VendorId, d.DeviceId, (unsigned long)d.AdapterLuid.HighPart,
                    (unsigned long)d.AdapterLuid.LowPart);
                for (UINT o = 0;; o++) {
                    IDXGIOutput *out = NULL;
                    char desc[96];
                    snprintf(desc, sizeof(desc), "device adapter EnumOutputs(%u)", o);
                    STEP(hr, desc, ad->EnumOutputs(o, &out));
                    if (FAILED(hr))
                        break;
                    UINT n = 0;
                    hr = out->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_ENUM_MODES_SCALING, &n, NULL);
                    say("    GetDisplayModeList(R8G8B8A8_UNORM_SRGB) 0x%08lx, %u modes", (unsigned long)hr, n);
                    out->Release();
                }
                ad->Release();
            }
            dxgi->Release();
        }
        D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS ho = {};
        STEP(hr, "CheckFeatureSupport(D3D10_X_HARDWARE_OPTIONS)",
             dev->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &ho, sizeof(ho)));
        say("  ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x=%d",
            ho.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x);
        release_dev(dev, dc, "EnumerateDevices");
    }
}

/* BuildMultiSampleQualityList: one device per combo, 32 sample counts. */
static void msaa_list(D3D_FEATURE_LEVEL fl, DXGI_FORMAT f, int windowed)
{
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *dc = NULL;
    D3D_FEATURE_LEVEL got;
    char what[96];
    snprintf(what, sizeof(what), "BuildMultiSampleQualityList %s windowed=%d", fmt_name(f), windowed);
    if (FAILED(create(g_type, &fl, 1, &dev, &got, &dc, what)))
        return;
    char line[512] = "";
    for (UINT i = 1; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i++) {
        UINT q = 0;
        HRESULT hr = dev->CheckMultisampleQualityLevels(f, i, &q);
        if (FAILED(hr) || q) {
            size_t l = strlen(line);
            snprintf(line + l, sizeof(line) - l, " %u:%s%u", i, FAILED(hr) ? "fail," : "", q);
        }
    }
    say("  CheckMultisampleQualityLevels(%s) count:quality%s", fmt_name(f), line[0] ? line : " none");
    release_dev(dev, dc, what);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* DXUTCreate3DEnvironment11 + DXUTCreateD3D11Views + render loop. */
static void create_and_present(IDXGIFactory1 *factory, IDXGIOutput *out, D3D_FEATURE_LEVEL fl, int frames)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"dxut_enum_probe";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r = {0, 0, 640, 480};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"dxut_enum_probe", L"vmlab-win3d probe: DXUT enumeration", WS_OVERLAPPEDWINDOW,
                              40, 40, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    HRESULT hr;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *dc = NULL;
    D3D_FEATURE_LEVEL got;
    if (FAILED(create(g_type, &fl, 1, &dev, &got, &dc, "DXUTCreate3DEnvironment11"))) {
        g_fatal = 1;
        return;
    }
    if (out) {
        DXGI_MODE_DESC want = {}, match = {};
        want.Width = 640;
        want.Height = 480;
        want.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        STEP(hr, "IDXGIOutput::FindClosestMatchingMode(640x480 R8G8B8A8_UNORM_SRGB, device)",
             out->FindClosestMatchingMode(&want, &match, dev));
        if (SUCCEEDED(hr))
            say("  closest %ux%u %u/%u %s", match.Width, match.Height, match.RefreshRate.Numerator,
                match.RefreshRate.Denominator, fmt_name(match.Format));
    }
    for (DXGI_FORMAT f : bb_formats) {
        UINT sup = 0;
        hr = dev->CheckFormatSupport(f, &sup);
        say("  CheckFormatSupport(%s) 0x%08lx support 0x%08x", fmt_name(f), (unsigned long)hr, sup);
    }
    for (DXGI_FORMAT f : ds_formats) {
        UINT sup = 0;
        hr = dev->CheckFormatSupport(f, &sup);
        say("  CheckFormatSupport(%s) 0x%08lx support 0x%08x", fmt_name(f), (unsigned long)hr, sup);
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = 640;
    sd.BufferDesc.Height = 480;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    IDXGISwapChain *swap = NULL;
    STEP(hr, "IDXGIFactory1::CreateSwapChain(640x480 R8G8B8A8_UNORM_SRGB, 2 buffers, DISCARD, ALLOW_MODE_SWITCH)",
         factory->CreateSwapChain(dev, &sd, &swap));
    if (FAILED(hr)) {
        g_fatal = 1;
        release_dev(dev, dc, "DXUTCreate3DEnvironment11");
        DestroyWindow(hwnd);
        return;
    }
    BOOL fs = FALSE;
    STEP(hr, "IDXGISwapChain::GetFullscreenState", swap->GetFullscreenState(&fs, NULL));
    ShowWindow(hwnd, SW_SHOW);
    pump();

    for (int pass = 0; pass < 2; pass++) {
        ID3D11Texture2D *bb = NULL;
        STEP(hr, "IDXGISwapChain::GetBuffer(0)", swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb));
        if (FAILED(hr)) {
            g_fatal = 1;
            break;
        }
        D3D11_TEXTURE2D_DESC bd;
        bb->GetDesc(&bd);
        ID3D11RenderTargetView *rtv = NULL;
        STEP(hr, "CreateRenderTargetView(back buffer)", dev->CreateRenderTargetView(bb, NULL, &rtv));
        D3D11_TEXTURE2D_DESC dd = {};
        dd.Width = bd.Width;
        dd.Height = bd.Height;
        dd.MipLevels = 1;
        dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dd.SampleDesc.Count = 1;
        dd.Usage = D3D11_USAGE_DEFAULT;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D *ds = NULL;
        ID3D11DepthStencilView *dsv = NULL;
        STEP(hr, "CreateTexture2D(D24_UNORM_S8_UINT depth)", dev->CreateTexture2D(&dd, NULL, &ds));
        if (ds)
            STEP(hr, "CreateDepthStencilView", dev->CreateDepthStencilView(ds, NULL, &dsv));
        dc->OMSetRenderTargets(1, &rtv, dsv);
        D3D11_VIEWPORT vp = {0, 0, (float)bd.Width, (float)bd.Height, 0, 1};
        dc->RSSetViewports(1, &vp);
        for (int i = 0; i < frames; i++) {
            const float c[4] = {0.1f, 0.4f * (i & 1), 0.6f, 1.0f};
            dc->ClearRenderTargetView(rtv, c);
            if (dsv)
                dc->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            if (i == 0 || i == frames - 1)
                STEP(hr, "IDXGISwapChain::Present(0, 0)", swap->Present(0, 0));
            else
                hr = swap->Present(0, 0);
            pump();
        }
        dc->OMSetRenderTargets(0, NULL, NULL);
        dc->ClearState();
        if (dsv)
            dsv->Release();
        if (ds)
            ds->Release();
        rtv->Release();
        bb->Release();
        if (pass == 0)
            STEP(hr, "IDXGISwapChain::ResizeBuffers(800x600)",
                 swap->ResizeBuffers(2, 800, 600, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                     DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
    }
    dc->ClearState();
    dc->Flush();
    say("  swap chain release ref %lu", swap->Release());
    release_dev(dev, dc, "DXUTCreate3DEnvironment11");
    DestroyWindow(hwnd);
    pump();
}

int wmain(int argc, wchar_t **argv)
{
    int frames = 120;
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--warp"))
            g_type = D3D_DRIVER_TYPE_WARP;
        else if (!wcscmp(argv[i], L"--log") && i + 1 < argc)
            g_log = _wfopen(argv[++i], L"w");
        else if (!wcscmp(argv[i], L"--frames") && i + 1 < argc)
            frames = _wtoi(argv[++i]);
    }
    say("dxut_enum_probe: driver type %s, %d-bit", g_type == D3D_DRIVER_TYPE_WARP ? "WARP" : "HARDWARE",
        (int)(sizeof(void *) * 8));

    HRESULT hr;
    IDXGIFactory1 *factory = NULL;
    STEP(hr, "CreateDXGIFactory1", CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory));
    if (FAILED(hr))
        return 1;

    D3D_FEATURE_LEVEL max_fl = (D3D_FEATURE_LEVEL)0;
    IDXGIOutput *first_out = NULL;
    for (UINT a = 0;; a++) {
        IDXGIAdapter *ad = NULL;
        char desc[96];
        snprintf(desc, sizeof(desc), "IDXGIFactory1::EnumAdapters(%u)", a);
        STEP(hr, desc, factory->EnumAdapters(a, &ad));
        if (FAILED(hr))
            break;
        DXGI_ADAPTER_DESC ad_desc = {};
        ad->GetDesc(&ad_desc);
        say("  adapter %u: %ls vendor 0x%04x device 0x%04x vram %llu MiB", a, ad_desc.Description,
            ad_desc.VendorId, ad_desc.DeviceId, (unsigned long long)(ad_desc.DedicatedVideoMemory >> 20));
        if (a == 0)
            enum_devices(&max_fl);
        int outputs = 0;
        for (UINT o = 0;; o++) {
            IDXGIOutput *out = NULL;
            snprintf(desc, sizeof(desc), "IDXGIAdapter::EnumOutputs(%u)", o);
            STEP(hr, desc, ad->EnumOutputs(o, &out));
            if (FAILED(hr))
                break;
            DXGI_OUTPUT_DESC od = {};
            out->GetDesc(&od);
            say("  output %u: %ls attached %d desktop %ld,%ld-%ld,%ld", o, od.DeviceName, od.AttachedToDesktop,
                od.DesktopCoordinates.left, od.DesktopCoordinates.top, od.DesktopCoordinates.right,
                od.DesktopCoordinates.bottom);
            int modes = enum_modes(out);
            if (modes > 0)
                outputs++;
            else
                say("  output %u has no display modes: DXUT drops it", o);
            if (a == 0 && !first_out && modes > 0)
                first_out = out;
            else
                out->Release();
        }
        say("  adapter %u: %d output(s) with modes%s", a, outputs,
            outputs ? "" : "  <-- DXUT drops this adapter (EnumerateOutputs)");
        ad->Release();
    }
    if (!max_fl) {
        say("no device from EnumerateDevices");
        return 1;
    }
    /* EnumerateDeviceCombos: for the hardware device on output 0, every back
     * buffer format that has modes, windowed and full screen. DXUT also does
     * the WARP device combos; one device type shows the pattern. */
    for (DXGI_FORMAT f : bb_formats)
        for (int w = 0; w < 2; w++)
            msaa_list(max_fl, f, w);

    create_and_present(factory, first_out, max_fl, frames);
    if (first_out)
        first_out->Release();
    say("factory release ref %lu", factory->Release());
    say("done%s", g_fatal ? " (a fatal step failed)" : "");
    if (g_log)
        fclose(g_log);
    return g_fatal;
}
