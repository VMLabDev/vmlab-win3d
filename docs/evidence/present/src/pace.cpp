// Presentation-rate repro for docs/present.md.
//
//   pace.exe <mode> <seconds> <out.txt> [width height]
//
// Modes:
//   d3d9        windowed D3D9, DISCARD, PresentationInterval ONE (as probes/d3d9)
//   d3d9-imm    the same with D3DPRESENT_INTERVAL_IMMEDIATE
//   d3d9ex      IDirect3D9Ex device, interval ONE
//   ddraw       DirectDraw DDSCL_NORMAL, clipper on the window, Blt from a
//               video-memory offscreen surface to the primary (as ddcaps'
//               windowed blit test)
//   vblank      no rendering; only the vblank thread below
//
// Every mode runs a thread that waits on D3DKMTWaitForVerticalBlankEvent for
// the window's adapter and source, so the KMD's vblank cadence is measured
// under the same load. The report gives the call rate, the time spent in
// each Present/Blt call, the frame interval, and the vblank interval
// (mean, p50, p95, max in ms). A line "result: PASS" or "result: FAIL"
// follows: a rendering mode passes when it reaches 50 calls per second.
#include <windows.h>
#include <d3d9.h>
#include <ddraw.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

typedef UINT KMTHANDLE;
struct KMT_OPENADAPTERFROMHDC { HDC hDc; KMTHANDLE hAdapter; LUID AdapterLuid; UINT VidPnSourceId; };
struct KMT_WAITFORVERTICALBLANKEVENT { KMTHANDLE hAdapter; KMTHANDLE hDevice; UINT VidPnSourceId; };
struct KMT_CLOSEADAPTER { KMTHANDLE hAdapter; };
typedef LONG(APIENTRY *PFN_OPEN)(KMT_OPENADAPTERFROMHDC *);
typedef LONG(APIENTRY *PFN_WAITVB)(const KMT_WAITFORVERTICALBLANKEVENT *);
typedef LONG(APIENTRY *PFN_CLOSE)(const KMT_CLOSEADAPTER *);

static LARGE_INTEGER g_freq;
static double now_ms() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return 1000.0 * (double)t.QuadPart / (double)g_freq.QuadPart;
}

struct Stats {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    std::string fmt() const {
        if (v.empty()) return "n=0";
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        double sum = 0;
        for (double x : s) sum += x;
        char b[160];
        snprintf(b, sizeof b, "n=%zu mean=%.2f p50=%.2f p95=%.2f max=%.2f", s.size(), sum / s.size(),
                 s[s.size() / 2], s[(size_t)(s.size() * 0.95)], s.back());
        return b;
    }
};

static std::atomic<bool> g_stop{false};
static Stats g_vblank;
static LONG g_vblank_err;

static DWORD WINAPI vblank_thread(void *arg) {
    HWND hwnd = (HWND)arg;
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    PFN_OPEN open = (PFN_OPEN)GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc");
    PFN_WAITVB wait = (PFN_WAITVB)GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent");
    PFN_CLOSE close = (PFN_CLOSE)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    if (!open || !wait) { g_vblank_err = -1; return 0; }
    HDC dc = GetDC(hwnd);
    KMT_OPENADAPTERFROMHDC oa = {};
    oa.hDc = dc;
    LONG st = open(&oa);
    ReleaseDC(hwnd, dc);
    if (st) { g_vblank_err = st; return 0; }
    KMT_WAITFORVERTICALBLANKEVENT w = {oa.hAdapter, 0, oa.VidPnSourceId};
    double last = -1;
    while (!g_stop) {
        st = wait(&w);
        if (st) { g_vblank_err = st; break; }
        double t = now_ms();
        if (last >= 0) g_vblank.add(t - last);
        last = t;
    }
    if (close) { KMT_CLOSEADAPTER ca = {oa.hAdapter}; close(&ca); }
    return 0;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) { g_stop = true; return 0; }
    return DefWindowProcW(h, m, w, l);
}

static void pump() {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

struct RhwVertex { float x, y, z, rhw; DWORD c; };

static int run_d3d9(HWND hwnd, int w, int h, bool ex, bool immediate, double seconds,
                    Stats &call, Stats &frame, int &count, std::string &err) {
    IDirect3D9 *d3d = NULL;
    IDirect3D9Ex *d3dex = NULL;
    if (ex) {
        if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3dex))) { err = "Direct3DCreate9Ex"; return 1; }
        d3d = d3dex;
    } else {
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) { err = "Direct3DCreate9"; return 1; }
    }
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = w;
    pp.BackBufferHeight = h;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.PresentationInterval = immediate ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_ONE;
    IDirect3DDevice9 *dev = NULL;
    HRESULT hr;
    if (ex) {
        IDirect3DDevice9Ex *devex = NULL;
        hr = d3dex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, NULL, &devex);
        dev = devex;
    } else {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                               D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    }
    if (FAILED(hr)) { char b[64]; snprintf(b, sizeof b, "CreateDevice 0x%08lx", hr); err = b; return 1; }
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    double t0 = now_ms(), last = -1;
    while (!g_stop && now_ms() - t0 < seconds * 1000.0) {
        pump();
        float a = (float)((now_ms() - t0) / 1000.0 * 2.0);
        dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 40, 90), 1.0f, 0);
        dev->BeginScene();
        RhwVertex v[3];
        for (int i = 0; i < 3; i++) {
            float t = a + i * 2.0943951f;
            v[i] = {w / 2 + cosf(t) * h * 0.4f, h / 2 + sinf(t) * h * 0.4f, 0.5f, 1.0f,
                    (DWORD)(i == 0 ? 0xffff4040 : i == 1 ? 0xff40ff40 : 0xff4040ff)};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof(RhwVertex));
        dev->EndScene();
        double c0 = now_ms();
        hr = dev->Present(NULL, NULL, NULL, NULL);
        double c1 = now_ms();
        if (FAILED(hr)) { char b[64]; snprintf(b, sizeof b, "Present 0x%08lx", hr); err = b; break; }
        call.add(c1 - c0);
        if (last >= 0) frame.add(c1 - last);
        last = c1;
        count++;
    }
    dev->Release();
    d3d->Release();
    return err.empty() ? 0 : 1;
}

static int run_ddraw(HWND hwnd, int w, int h, double seconds, Stats &call, Stats &frame, int &count,
                     std::string &err) {
    IDirectDraw7 *dd = NULL;
    HRESULT hr = DirectDrawCreateEx(NULL, (void **)&dd, IID_IDirectDraw7, NULL);
    if (FAILED(hr)) { err = "DirectDrawCreateEx"; return 1; }
    dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    DDSURFACEDESC2 sd = {};
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDrawSurface7 *primary = NULL, *off = NULL;
    if (FAILED(hr = dd->CreateSurface(&sd, &primary, NULL))) { err = "CreateSurface(primary)"; return 1; }
    IDirectDrawClipper *clip = NULL;
    dd->CreateClipper(0, &clip, NULL);
    clip->SetHWnd(0, hwnd);
    primary->SetClipper(clip);
    sd = {};
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    sd.dwWidth = w;
    sd.dwHeight = h;
    if (FAILED(hr = dd->CreateSurface(&sd, &off, NULL))) { err = "CreateSurface(offscreen)"; return 1; }
    double t0 = now_ms(), last = -1;
    int i = 0;
    while (!g_stop && now_ms() - t0 < seconds * 1000.0) {
        pump();
        DDBLTFX fx = {};
        fx.dwSize = sizeof fx;
        fx.dwFillColor = (i++ & 1) ? 0x00204080 : 0x00804020;
        RECT bar = {0, 0, w, h};
        off->Blt(&bar, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
        POINT p = {0, 0};
        ClientToScreen(hwnd, &p);
        RECT dst = {p.x, p.y, p.x + w, p.y + h};
        double c0 = now_ms();
        hr = primary->Blt(&dst, off, NULL, DDBLT_WAIT, NULL);
        double c1 = now_ms();
        if (FAILED(hr)) { char b[64]; snprintf(b, sizeof b, "Blt 0x%08lx", hr); err = b; break; }
        call.add(c1 - c0);
        if (last >= 0) frame.add(c1 - last);
        last = c1;
        count++;
    }
    off->Release();
    clip->Release();
    primary->Release();
    dd->Release();
    return err.empty() ? 0 : 1;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: pace <d3d9|d3d9-imm|d3d9ex|ddraw|vblank> <seconds> <out.txt> [width height]\n");
        return 2;
    }
    QueryPerformanceFrequency(&g_freq);
    char mode[32];
    WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, mode, sizeof mode, NULL, NULL);
    double seconds = _wtof(argv[2]);
    FILE *out = _wfopen(argv[3], L"w");
    if (!out) return 2;
    SetProcessDPIAware();
    const int W = argc > 5 ? _wtoi(argv[4]) : 640, H = argc > 5 ? _wtoi(argv[5]) : 480;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"pace";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r = {0, 0, W, H};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    wchar_t title[64];
    swprintf(title, 64, L"pace %ls", argv[1]);
    HWND hwnd = CreateWindowW(L"pace", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    SetForegroundWindow(hwnd);
    pump();

    HANDLE vt = CreateThread(NULL, 0, vblank_thread, hwnd, 0, NULL);
    Stats call, frame;
    int count = 0;
    std::string err;
    double t0 = now_ms();
    int rc = 0;
    std::string m = mode;
    if (m == "d3d9") rc = run_d3d9(hwnd, W, H, false, false, seconds, call, frame, count, err);
    else if (m == "d3d9-imm") rc = run_d3d9(hwnd, W, H, false, true, seconds, call, frame, count, err);
    else if (m == "d3d9ex") rc = run_d3d9(hwnd, W, H, true, false, seconds, call, frame, count, err);
    else if (m == "ddraw") rc = run_ddraw(hwnd, W, H, seconds, call, frame, count, err);
    else if (m == "vblank") {
        while (!g_stop && now_ms() - t0 < seconds * 1000.0) { pump(); Sleep(10); }
    } else { fprintf(out, "unknown mode %s\n", mode); return 2; }
    double el = (now_ms() - t0) / 1000.0;
    g_stop = true;
    WaitForSingleObject(vt, 1000);

    double rate = count / el;
    fprintf(out, "mode: %s\n", mode);
    fprintf(out, "size: %dx%d\n", W, H);
    fprintf(out, "seconds: %.2f\n", el);
    fprintf(out, "calls: %d\n", count);
    fprintf(out, "rate_per_s: %.1f\n", rate);
    fprintf(out, "call_ms: %s\n", call.fmt().c_str());
    fprintf(out, "frame_ms: %s\n", frame.fmt().c_str());
    fprintf(out, "vblank_ms: %s\n", g_vblank.fmt().c_str());
    if (g_vblank_err) fprintf(out, "vblank_error: 0x%08lx\n", (unsigned long)g_vblank_err);
    if (!err.empty()) fprintf(out, "error: %s\n", err.c_str());
    bool pass = m == "vblank" ? g_vblank.v.size() > 0 : (rc == 0 && rate >= 50.0);
    fprintf(out, "result: %s\n", pass ? "PASS" : "FAIL");
    fclose(out);
    DestroyWindow(hwnd);
    return pass ? 0 : 1;
}
