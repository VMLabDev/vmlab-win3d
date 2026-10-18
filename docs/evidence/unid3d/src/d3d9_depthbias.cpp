/* unid3d F5: D3D9 depth bias (D3DRS_DEPTHBIAS) on each depth format.
 *
 * For each depth-stencil format the HAL device accepts, the probe clears a
 * 64x64 A8R8G8B8 target to black and depth to 1.0, draws a red quad at
 * depth Z0 = 0.5, then draws a green quad at depth Z0 + dz with a constant
 * depth bias b (D3DCMP_LESSEQUAL, pretransformed vertices, so the depth is
 * exactly the vertex z). The green quad shows where Z0 + dz + b <= Z0, so
 * each case has an expected colour:
 *
 *   dz = +0.004  b = -0.008  green (the bias pulls it in front)
 *   dz = +0.004  b = -0.002  red   (not far enough)
 *   dz = +0.004  b =  0      red   (no bias)
 *   dz = -0.004  b = +0.008  red   (the bias pushes it behind)
 *   dz = -0.004  b = +0.002  green
 *
 * The depths are 0.004 apart, far above the resolution of D16 (2^-16), so
 * any representation of the bias that is roughly right gets every case.
 * A draw that the driver skips leaves red where green is expected.
 *
 * After the cases the probe measures the bias the device applies: it
 * bisects for the smallest bias b that brings the quad at Z0 + 0.004 in
 * front, and logs scale = 0.004 / b, the factor by which the device's bias
 * differs from D3D9's (1.0 on the reference rasterizer; a device that
 * applies twice the bias gives 2.0).
 *
 *   d3d9_depthbias.exe [--log FILE] [--ref]
 *
 * Log lines:
 *   CASE fmt=<name> dz=<dz> bias=<b> expect=<colour> got=<AARRGGBB> hr=<first failure> <ok|FAIL>
 *   SCALE fmt=<name> threshold=<b> scale=<0.004 / b>
 *   RESULT ran=<n> failed=<n>
 * Exit code: the number of failed cases.
 */
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

#define W 64
#define H 64

static FILE *logf_;
static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf_, fmt, ap);
    va_end(ap);
    fflush(logf_);
}

static DWORD fbits(float f) { DWORD d; memcpy(&d, &f, 4); return d; }

struct TV { float x, y, z, rhw; DWORD c; };

static void quad(IDirect3DDevice9 *dev, float z, DWORD c)
{
    TV v[4] = {
        { -0.5f, -0.5f, z, 1.0f, c }, { W - 0.5f, -0.5f, z, 1.0f, c },
        { -0.5f, H - 0.5f, z, 1.0f, c }, { W - 0.5f, H - 0.5f, z, 1.0f, c },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TV));
}

/* Red quad at 0.5, then a green one at 0.5 + dz with depth bias b; returns
 * the centre pixel. */
static DWORD draw_case(IDirect3DDevice9 *dev, IDirect3DSurface9 *rt, IDirect3DSurface9 *sys,
                       float dz, float bias, HRESULT *first)
{
#define CK(e) do { HRESULT h_ = (e); if (FAILED(h_) && SUCCEEDED(*first)) *first = h_; } while (0)
    CK(dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, 1.0f, 0));
    CK(dev->BeginScene());
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_DEPTHBIAS, fbits(0.0f));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, fbits(0.0f));
    quad(dev, 0.5f, 0xffff0000);
    dev->SetRenderState(D3DRS_DEPTHBIAS, fbits(bias));
    quad(dev, 0.5f + dz, 0xff00ff00);
    dev->SetRenderState(D3DRS_DEPTHBIAS, fbits(0.0f));
    CK(dev->EndScene());
    CK(dev->GetRenderTargetData(rt, sys));
#undef CK
    DWORD px = 0;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(sys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
        px = ((DWORD *)((BYTE *)lr.pBits + (H / 2) * lr.Pitch))[W / 2];
        sys->UnlockRect();
    }
    return px;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *logpath = NULL;
    D3DDEVTYPE type = D3DDEVTYPE_HAL;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--log") && i + 1 < argc) logpath = argv[++i];
        else if (!wcscmp(argv[i], L"--ref")) type = D3DDEVTYPE_REF;
    }
    logf_ = logpath ? _wfopen(logpath, L"w") : stdout;
    if (!logf_) return 100;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"d3d9_depthbias";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"d3d9_depthbias", WS_OVERLAPPEDWINDOW,
                              0, 0, 256, 256, NULL, NULL, wc.hInstance, NULL);
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { logf("Direct3DCreate9 failed\n"); return 101; }
    D3DADAPTER_IDENTIFIER9 id;
    if (SUCCEEDED(d3d->GetAdapterIdentifier(0, 0, &id)))
        logf("ADAPTER %s type=%s\n", id.Description, type == D3DDEVTYPE_REF ? "ref" : "hal");

    static const struct { D3DFORMAT f; const char *name; } fmts[] = {
        { D3DFMT_D16, "D16" },
        { D3DFMT_D24X8, "D24X8" },
        { D3DFMT_D24S8, "D24S8" },
        { D3DFMT_D32, "D32" },
        { D3DFMT_D32F_LOCKABLE, "D32F_LOCKABLE" },
        { D3DFMT_D24FS8, "D24FS8" },
    };
    static const struct { float dz, bias; bool green; } cases[] = {
        { +0.004f, -0.008f, true }, { +0.004f, -0.002f, false }, { +0.004f, 0.0f, false },
        { -0.004f, +0.008f, false }, { -0.004f, +0.002f, true },
    };
    int ran = 0, failed = 0;
    for (const auto &fm : fmts) {
        if (FAILED(d3d->CheckDeviceFormat(0, type, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL,
                                          D3DRTYPE_SURFACE, fm.f))) {
            logf("SKIP fmt=%s (not supported)\n", fm.name);
            continue;
        }
        D3DPRESENT_PARAMETERS pp = {};
        pp.BackBufferWidth = W;
        pp.BackBufferHeight = H;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = hwnd;
        pp.Windowed = TRUE;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = fm.f;
        IDirect3DDevice9 *dev = NULL;
        HRESULT hr = d3d->CreateDevice(0, type, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
        if (FAILED(hr)) {
            logf("SKIP fmt=%s CreateDevice hr=0x%08lx\n", fm.name, (unsigned long)hr);
            continue;
        }
        IDirect3DSurface9 *rt = NULL, *sys = NULL;
        dev->GetRenderTarget(0, &rt);
        dev->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL);
        for (const auto &cs : cases) {
            HRESULT first = S_OK;
            const DWORD px = draw_case(dev, rt, sys, cs.dz, cs.bias, &first);
            const DWORD want = cs.green ? 0xff00ff00 : 0xffff0000;
            const bool ok = SUCCEEDED(first) && (px & 0xffffff) == (want & 0xffffff);
            ran++;
            if (!ok) failed++;
            logf("CASE fmt=%s dz=%+.3f bias=%+.3f expect=%s got=%08lx hr=0x%08lx %s\n", fm.name, cs.dz, cs.bias,
                 cs.green ? "green" : "red", (unsigned long)px, (unsigned long)first, ok ? "ok" : "FAIL");
        }
        /* Bisect for the smallest bias that wins against dz = 0.004. */
        float lo = 0.0f, hi = 0.02f;
        HRESULT first = S_OK;
        if ((draw_case(dev, rt, sys, 0.004f, -hi, &first) & 0xffffff) == 0x00ff00) {
            for (int i = 0; i < 24; i++) {
                const float mid = 0.5f * (lo + hi);
                if ((draw_case(dev, rt, sys, 0.004f, -mid, &first) & 0xffffff) == 0x00ff00) hi = mid;
                else lo = mid;
            }
            logf("SCALE fmt=%s threshold=%.6f scale=%.3f\n", fm.name, hi, 0.004f / hi);
        } else {
            logf("SCALE fmt=%s threshold=none (a bias of 0.02 does not win) hr=0x%08lx\n", fm.name, (unsigned long)first);
        }
        if (sys) sys->Release();
        if (rt) rt->Release();
        dev->Release();
    }
    logf("RESULT ran=%d failed=%d\n", ran, failed);
    d3d->Release();
    DestroyWindow(hwnd);
    return failed;
}
