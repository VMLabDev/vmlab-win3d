/* Xid 69 bisect probe: D3D9 indexed draws in the forms the DirectX SDK
 * samples use, one form per run, so the one the RTX 5090 refuses can be
 * named. Fixed-function only (pre-transformed XYZRHW|DIFFUSE quads), so it
 * needs no shader compiler.
 *
 *   d3d9_indexed.exe <mode> [seconds] [depth]
 *
 * Every frame draws QUADS quads (6 * QUADS indices, 168 for 28 quads, the
 * count in the Xid) in a grid. Modes:
 *   static16    managed 16-bit index buffer, DrawIndexedPrimitive
 *   static32    managed 32-bit index buffer
 *   up16        DrawIndexedPrimitiveUP, 16-bit indices
 *   up32        DrawIndexedPrimitiveUP, 32-bit indices
 *   dynamic16   dynamic 16-bit index buffer refilled with NOOVERWRITE at a
 *               rolling offset, StartIndex != 0 (the ID3DXSprite pattern)
 *   basevertex  static16 with BaseVertexIndex > 0 into a bigger vertex buffer
 *   dynvb       static16 indices, dynamic vertex buffer with NOOVERWRITE
 *               and BaseVertexIndex at the rolling offset (ID3DXFont)
 *   strip16     static 16-bit indices, D3DPT_TRIANGLESTRIP
 * depth: none (default), d16, d24x8, d24s8 or d32: an auto depth-stencil
 * buffer of that format, cleared each frame, with depth test LESSEQUAL and
 * depth writes on (BasicHLSL's DXUT device has D24X8).
 * Writes <exe dir>\d3d9_indexed-<mode>.txt with each HRESULT and the frame
 * count, flushed every second, so a hang leaves the last state on disk.
 */
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

#define QUADS 28
#define W 640
#define H 480

struct Vtx { float x, y, z, rhw; DWORD c; };
#define FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static FILE *logf_;
static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf_, fmt, ap);
    va_end(ap);
    fflush(logf_);
}
static bool check(HRESULT hr, const char *what)
{
    if (FAILED(hr)) { logf("FAIL %s hr=0x%08lx\n", what, (unsigned long)hr); return true; }
    return false;
}

static void quad_vertices(Vtx *v, int frame)
{
    for (int q = 0; q < QUADS; q++) {
        float x = 20.0f + (q % 7) * 85.0f, y = 40.0f + (q / 7) * 100.0f;
        float s = 60.0f + 10.0f * (float)((frame / 10 + q) % 3);
        DWORD c = D3DCOLOR_XRGB(60 + q * 7, 255 - q * 8, (frame * 4) & 255);
        Vtx *p = v + q * 4;
        p[0] = { x, y, 0.5f, 1.0f, c };
        p[1] = { x + s, y, 0.5f, 1.0f, c };
        p[2] = { x, y + s, 0.5f, 1.0f, c };
        p[3] = { x + s, y + s, 0.5f, 1.0f, c };
    }
}
template <typename T> static void quad_indices(T *ix)
{
    for (int q = 0; q < QUADS; q++) {
        T b = (T)(q * 4);
        T *p = ix + q * 6;
        p[0] = b; p[1] = b + 1; p[2] = b + 2; p[3] = b + 2; p[4] = b + 1; p[5] = b + 3;
    }
}
/* Strip: degenerate-joined quads, 4 + 2 indices per quad except the last. */
static UINT strip_indices(WORD *ix)
{
    UINT n = 0;
    for (int q = 0; q < QUADS; q++) {
        WORD b = (WORD)(q * 4);
        if (q) { WORD prev = ix[n - 1]; ix[n++] = prev; ix[n++] = b; }
        ix[n++] = b; ix[n++] = b + 1; ix[n++] = b + 2; ix[n++] = b + 3;
    }
    return n;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char mode[32] = "static16";
    int seconds = 20;
    if (argc > 1) WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, mode, sizeof mode, NULL, NULL);
    if (argc > 2) seconds = _wtoi(argv[2]);
    char depth[16] = "none";
    if (argc > 3) WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, depth, sizeof depth, NULL, NULL);
    D3DFORMAT zfmt = D3DFMT_UNKNOWN;
    if (!strcmp(depth, "d16")) zfmt = D3DFMT_D16;
    else if (!strcmp(depth, "d24x8")) zfmt = D3DFMT_D24X8;
    else if (!strcmp(depth, "d24s8")) zfmt = D3DFMT_D24S8;
    else if (!strcmp(depth, "d32")) zfmt = D3DFMT_D32;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    *wcsrchr(path, L'\\') = 0;
    wchar_t logpath[MAX_PATH];
    swprintf(logpath, MAX_PATH, L"%ls\\d3d9_indexed-%hs-%hs.txt", path, mode, depth);
    logf_ = _wfopen(logpath, L"w");
    if (!logf_) return 2;
    logf("mode %s depth %s quads %d indices %d seconds %d\n", mode, depth, QUADS, QUADS * 6, seconds);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = L"d3d9_indexed";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r = { 0, 0, W, H };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    wchar_t title[64];
    swprintf(title, 64, L"d3d9_indexed %hs", mode);
    HWND hwnd = CreateWindowW(L"d3d9_indexed", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              40, 40, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { logf("FAIL Direct3DCreate9\n"); return 1; }
    D3DADAPTER_IDENTIFIER9 id = {};
    d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    logf("adapter %s\n", id.Description);
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (zfmt != D3DFMT_UNKNOWN) {
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = zfmt;
    }
    IDirect3DDevice9 *dev = NULL;
    if (check(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev), "CreateDevice"))
        return 1;

    const bool is32 = !strcmp(mode, "static32") || !strcmp(mode, "up32");
    const bool dynib = !strcmp(mode, "dynamic16");
    const bool dynvb = !strcmp(mode, "dynvb");
    const bool basev = !strcmp(mode, "basevertex");
    const bool up = !strncmp(mode, "up", 2);
    const bool strip = !strcmp(mode, "strip16");
    const UINT nverts = QUADS * 4;
    const UINT ring = 16; /* dynamic buffers hold this many frames' worth */

    IDirect3DVertexBuffer9 *vb = NULL;
    IDirect3DIndexBuffer9 *ib = NULL;
    Vtx verts[QUADS * 4];
    WORD ix16[QUADS * 6 + QUADS * 2];
    DWORD ix32[QUADS * 6];
    quad_indices(ix16);
    quad_indices(ix32);
    UINT strip_count = strip ? strip_indices(ix16) : 0;

    if (!up) {
        UINT vcount = (dynvb || basev) ? nverts * ring : nverts;
        DWORD vusage = dynvb ? (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY) : D3DUSAGE_WRITEONLY;
        D3DPOOL vpool = dynvb ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
        if (check(dev->CreateVertexBuffer(vcount * sizeof(Vtx), vusage, FVF, vpool, &vb, NULL),
                  "CreateVertexBuffer"))
            return 1;
        if (!dynvb) {
            void *p;
            if (check(vb->Lock(0, 0, &p, 0), "vb Lock")) return 1;
            for (UINT i = 0; i < vcount / nverts; i++) { quad_vertices(verts, 0); memcpy((Vtx *)p + i * nverts, verts, sizeof verts); }
            vb->Unlock();
        }
        UINT isize = is32 ? sizeof ix32 : (dynib ? sizeof(WORD) * QUADS * 6 * ring : sizeof ix16);
        DWORD iusage = dynib ? (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY) : D3DUSAGE_WRITEONLY;
        D3DPOOL ipool = dynib ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
        if (check(dev->CreateIndexBuffer(isize, iusage, is32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16,
                                         ipool, &ib, NULL), "CreateIndexBuffer"))
            return 1;
        if (!dynib) {
            void *p;
            if (check(ib->Lock(0, 0, &p, 0), "ib Lock")) return 1;
            memcpy(p, is32 ? (void *)ix32 : (void *)ix16, is32 ? sizeof ix32 : sizeof ix16);
            ib->Unlock();
        }
    }

    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, zfmt != D3DFMT_UNKNOWN ? D3DZB_TRUE : D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetFVF(FVF);

    DWORD start = GetTickCount(), last = start;
    int frame = 0, fails = 0;
    MSG msg;
    while (GetTickCount() - start < (DWORD)seconds * 1000) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto out;
            DispatchMessageW(&msg);
        }
        dev->Clear(0, NULL, D3DCLEAR_TARGET | (zfmt != D3DFMT_UNKNOWN ? D3DCLEAR_ZBUFFER : 0) |
                   (zfmt == D3DFMT_D24S8 ? D3DCLEAR_STENCIL : 0), D3DCOLOR_XRGB(20, 20, 40), 1.0f, 0);
        dev->BeginScene();
        quad_vertices(verts, frame);
        HRESULT hr = S_OK;
        UINT slot = frame % ring;
        if (up) {
            hr = dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nverts, QUADS * 2,
                                             is32 ? (void *)ix32 : (void *)ix16,
                                             is32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16,
                                             verts, sizeof(Vtx));
        } else {
            INT base = 0;
            UINT startidx = 0;
            if (dynib) {
                void *p;
                UINT off = slot * sizeof(WORD) * QUADS * 6;
                hr = ib->Lock(off, sizeof(WORD) * QUADS * 6, &p, slot ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD);
                if (SUCCEEDED(hr)) { memcpy(p, ix16, sizeof(WORD) * QUADS * 6); ib->Unlock(); }
                startidx = slot * QUADS * 6;
            }
            if (dynvb) {
                void *p;
                hr = vb->Lock(slot * sizeof verts, sizeof verts, &p, slot ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD);
                if (SUCCEEDED(hr)) { memcpy(p, verts, sizeof verts); vb->Unlock(); }
                base = (INT)(slot * nverts);
            }
            if (basev) base = (INT)(slot * nverts);
            dev->SetStreamSource(0, vb, 0, sizeof(Vtx));
            dev->SetIndices(ib);
            if (SUCCEEDED(hr)) {
                if (strip)
                    hr = dev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0, nverts, 0, strip_count - 2);
                else
                    hr = dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, base, 0, nverts, startidx, QUADS * 2);
            }
        }
        if (FAILED(hr) && fails++ < 5) logf("FAIL draw frame %d hr=0x%08lx\n", frame, (unsigned long)hr);
        dev->EndScene();
        hr = dev->Present(NULL, NULL, NULL, NULL);
        if (FAILED(hr) && fails++ < 5) logf("FAIL Present frame %d hr=0x%08lx\n", frame, (unsigned long)hr);
        frame++;
        if (GetTickCount() - last >= 1000) { last = GetTickCount(); logf("t=%lu frames=%d\n", (unsigned long)(last - start) / 1000, frame); }
    }
out:
    logf("done frames=%d fails=%d\n", frame, fails);
    if (ib) ib->Release();
    if (vb) vb->Release();
    dev->Release();
    d3d->Release();
    fclose(logf_);
    return fails ? 1 : 0;
}
