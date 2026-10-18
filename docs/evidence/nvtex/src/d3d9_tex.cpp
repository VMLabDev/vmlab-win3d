/* nvtex repro: one textured quad sampled from a mipmapped texture, with a
 * pixel readback, so the cases that render black on NVIDIA can be named.
 *
 *   d3d9_tex.exe [key=value ...]     one case
 *   d3d9_tex.exe matrix [key=value]  the built-in case list, one device each
 *
 * Every mip level of the 256x256 texture is one solid colour (level 0 red,
 * 1 green, 2 blue, 3 yellow, 4 magenta, 5 cyan, 6 white, 7 orange, 8 grey),
 * so the colour at the centre of the quad names the level that was sampled.
 * Keys (default first):
 *   path=hlsl|hlsl3|ff-rhw|ff-rhw-mod|ff-rhw-diffuse|ff-xyz|ff-xyz-lit
 *        hlsl:  vs_2_0/ps_2_0 compiled at run time by d3dcompiler_47.dll
 *        hlsl3: vs_3_0/ps_3_0
 *        ff-rhw: fixed function, XYZRHW|TEX1, COLOROP SELECTARG1 TEXTURE
 *        ff-rhw-mod: XYZRHW|TEX1, COLOROP MODULATE TEXTURE DIFFUSE (no
 *                diffuse in the vertex: white by default)
 *        ff-rhw-diffuse: XYZRHW|DIFFUSE|TEX1, COLOROP MODULATE
 *        ff-xyz: XYZ|TEX1, identity transforms, lighting off
 *        ff-xyz-lit: XYZ|NORMAL|TEX1, one directional light, white material
 *   fmt=x8|a8|dxt1|r5g6b5|a1r5g5b5  D3DFMT_X8R8G8B8 (X byte 0), A8R8G8B8, DXT1,
 *                                R5G6B5, A1R5G5B5
 *   pool=managed|default|dynamic default: SYSTEMMEM texture + UpdateTexture
 *   levels=0|<n>                 0 = full chain (9 levels)
 *   mip=linear|point|none        D3DSAMP_MIPFILTER (MIN/MAG are LINEAR)
 *   maxmip=0|<n>                 D3DSAMP_MAXMIPLEVEL
 *   bias=0|<float>               D3DSAMP_MIPMAPLODBIAS
 *   size=64|<n>                  quad edge in pixels (256 samples level 0)
 *   depth=none|d24x8             auto depth-stencil, cleared, LESSEQUAL test
 *   frames=60|<n>                frames presented after the readback
 * Writes <exe dir>\d3d9_tex.txt (appends): one line per case with every
 * failed HRESULT, the centre pixel, the level it matches and PASS or FAIL.
 * Exit code: number of failed cases.
 */
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 512
#define H 512
#define TEX 256

static FILE *logf_;
static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf_, fmt, ap);
    va_end(ap);
    fflush(logf_);
}

static const DWORD palette[9] = {
    0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff, 0xffffff, 0xff8000, 0x808080,
};

struct Config {
    char path[24] = "hlsl";
    char fmt[16] = "x8";
    char pool[16] = "managed";
    int levels = 0;
    char mip[16] = "linear";
    int maxmip = 0;
    float bias = 0.0f;
    int size = 64;
    char depth[16] = "none";
    int frames = 60;
};

static bool set_key(Config &c, const char *kv)
{
    const char *eq = strchr(kv, '=');
    if (!eq) return false;
    char k[32];
    size_t n = (size_t)(eq - kv);
    if (n >= sizeof k) return false;
    memcpy(k, kv, n); k[n] = 0;
    const char *v = eq + 1;
#define S(name) if (!strcmp(k, #name)) { snprintf(c.name, sizeof c.name, "%s", v); return true; }
#define I(name) if (!strcmp(k, #name)) { c.name = atoi(v); return true; }
    S(path) S(fmt) S(pool) S(mip) S(depth)
    I(levels) I(maxmip) I(size) I(frames)
    if (!strcmp(k, "bias")) { c.bias = (float)atof(v); return true; }
#undef S
#undef I
    return false;
}

static void describe(const Config &c, char *out, size_t n)
{
    snprintf(out, n, "path=%s fmt=%s pool=%s levels=%d mip=%s maxmip=%d bias=%g size=%d depth=%s",
             c.path, c.fmt, c.pool, c.levels, c.mip, c.maxmip, c.bias, c.size, c.depth);
}

/* The level the sampler should pick at the quad's centre. */
static int expected_level(const Config &c, int nlevels)
{
    float lod = log2f((float)TEX / (float)c.size) + c.bias;
    int l = (int)floorf(lod + 0.5f);
    if (!strcmp(c.mip, "none")) l = 0;
    if (l < c.maxmip) l = c.maxmip;
    if (l < 0) l = 0;
    if (l > nlevels - 1) l = nlevels - 1;
    return l;
}

static int nearest_level(DWORD px, int *dist)
{
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < 9; i++) {
        int dr = (int)((px >> 16) & 255) - (int)((palette[i] >> 16) & 255);
        int dg = (int)((px >> 8) & 255) - (int)((palette[i] >> 8) & 255);
        int db = (int)(px & 255) - (int)(palette[i] & 255);
        int d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = i; }
    }
    *dist = bd;
    return best;
}

static WORD rgb565(DWORD c)
{
    return (WORD)((((c >> 19) & 31) << 11) | (((c >> 10) & 63) << 5) | ((c >> 3) & 31));
}

static void fill_level(D3DFORMAT f, const D3DLOCKED_RECT &lr, UINT dim, DWORD colour)
{
    BYTE *row = (BYTE *)lr.pBits;
    if (f == D3DFMT_DXT1) {
        UINT blocks = (dim + 3) / 4;
        for (UINT y = 0; y < blocks; y++, row += lr.Pitch) {
            WORD *b = (WORD *)row;
            for (UINT x = 0; x < blocks; x++) {
                b[x * 4 + 0] = rgb565(colour);
                b[x * 4 + 1] = rgb565(colour);
                b[x * 4 + 2] = 0;
                b[x * 4 + 3] = 0;
            }
        }
        return;
    }
    for (UINT y = 0; y < dim; y++, row += lr.Pitch) {
        for (UINT x = 0; x < dim; x++) {
            if (f == D3DFMT_R5G6B5)
                ((WORD *)row)[x] = rgb565(colour);
            else if (f == D3DFMT_A1R5G5B5)
                ((WORD *)row)[x] = (WORD)(0x8000 | (((colour >> 19) & 31) << 10) |
                                          (((colour >> 11) & 31) << 5) | ((colour >> 3) & 31));
            else if (f == D3DFMT_X8R8G8B8)
                ((DWORD *)row)[x] = colour & 0xffffff; /* X byte 0 */
            else
                ((DWORD *)row)[x] = 0xff000000 | colour;
        }
    }
}

typedef HRESULT(WINAPI *PFN_D3DCompile)(const void *, SIZE_T, const char *, const void *, void *,
                                        const char *, const char *, UINT, UINT, void **, void **);
/* ID3DBlob without d3dcommon.h: QueryInterface, AddRef, Release, GetBufferPointer, GetBufferSize. */
struct IBlob {
    virtual HRESULT WINAPI QueryInterface(REFIID, void **) = 0;
    virtual ULONG WINAPI AddRef() = 0;
    virtual ULONG WINAPI Release() = 0;
    virtual void *WINAPI GetBufferPointer() = 0;
    virtual SIZE_T WINAPI GetBufferSize() = 0;
};

static const char vs_src[] =
    "struct O { float4 p : POSITION; float2 t : TEXCOORD0; };\n"
    "O main(float4 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = float4(p.xy, 0.5, 1); o.t = t; return o; }\n";
static const char ps_src[] =
    "sampler2D s : register(s0);\n"
    "float4 main(float2 t : TEXCOORD0) : COLOR { return tex2D(s, t); }\n";

static IBlob *compile(const char *src, const char *profile)
{
    static PFN_D3DCompile fn;
    if (!fn) {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!m) m = LoadLibraryW(L"d3dcompiler_43.dll");
        if (m) fn = (PFN_D3DCompile)GetProcAddress(m, "D3DCompile");
        if (!fn) { logf("  no D3DCompile\n"); return NULL; }
    }
    IBlob *code = NULL, *err = NULL;
    HRESULT hr = fn(src, strlen(src), "nvtex", NULL, NULL, "main", profile, 0, 0, (void **)&code, (void **)&err);
    if (FAILED(hr)) {
        logf("  D3DCompile %s hr=0x%08lx %s\n", profile, (unsigned long)hr,
             err ? (const char *)err->GetBufferPointer() : "");
        return NULL;
    }
    return code;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

#define CHECK(expr, what) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    snprintf(err + strlen(err), sizeof err - strlen(err), " %s=0x%08lx", what, (unsigned long)hr_); } } while (0)

struct VXyz { float x, y, z; float u, v; };
struct VXyzN { float x, y, z; float nx, ny, nz; float u, v; };
struct VRhw { float x, y, z, rhw; float u, v; };
struct VRhwD { float x, y, z, rhw; DWORD c; float u, v; };

/* Runs one case. Returns true on PASS. */
static bool run_case(IDirect3D9 *d3d, HWND hwnd, const Config &c)
{
    char desc[256], err[512] = "";
    describe(c, desc, sizeof desc);
    D3DFORMAT zfmt = !strcmp(c.depth, "d24x8") ? D3DFMT_D24X8 : D3DFMT_UNKNOWN;
    D3DFORMAT tfmt = !strcmp(c.fmt, "a8") ? D3DFMT_A8R8G8B8 : !strcmp(c.fmt, "dxt1") ? D3DFMT_DXT1 :
                     !strcmp(c.fmt, "r5g6b5") ? D3DFMT_R5G6B5 :
                     !strcmp(c.fmt, "a1r5g5b5") ? D3DFMT_A1R5G5B5 : D3DFMT_X8R8G8B8;

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
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { logf("FAIL %s CreateDevice=0x%08lx\n", desc, (unsigned long)hr); return false; }

    /* Texture. */
    IDirect3DTexture9 *tex = NULL, *stage = NULL;
    const bool deflt = !strcmp(c.pool, "default"), dyn = !strcmp(c.pool, "dynamic");
    D3DPOOL pool = (deflt || dyn) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
    CHECK(dev->CreateTexture(TEX, TEX, c.levels, dyn ? D3DUSAGE_DYNAMIC : 0, tfmt, pool, &tex, NULL),
          "CreateTexture");
    int nlevels = tex ? (int)tex->GetLevelCount() : 0;
    IDirect3DTexture9 *fill = tex;
    if (tex && deflt) {
        CHECK(dev->CreateTexture(TEX, TEX, c.levels, 0, tfmt, D3DPOOL_SYSTEMMEM, &stage, NULL),
              "CreateTexture(SYSTEMMEM)");
        fill = stage;
    }
    for (int l = 0; fill && l < nlevels; l++) {
        D3DLOCKED_RECT lr;
        HRESULT lh = fill->LockRect(l, &lr, NULL, 0);
        if (FAILED(lh)) { CHECK(lh, "LockRect"); break; }
        fill_level(tfmt, lr, TEX >> l, palette[l < 9 ? l : 8]);
        fill->UnlockRect(l);
    }
    if (stage) CHECK(dev->UpdateTexture(stage, tex), "UpdateTexture");

    /* Geometry and shaders. */
    float half = (float)c.size / 2.0f;
    float x0 = W / 2.0f - half, y0 = H / 2.0f - half, x1 = x0 + c.size, y1 = y0 + c.size;
    float cx0 = x0 / W * 2 - 1, cx1 = x1 / W * 2 - 1, cy0 = 1 - y0 / H * 2, cy1 = 1 - y1 / H * 2;
    VXyz qx[4] = { { cx0, cy0, 0.5f, 0, 0 }, { cx1, cy0, 0.5f, 1, 0 }, { cx0, cy1, 0.5f, 0, 1 }, { cx1, cy1, 0.5f, 1, 1 } };
    VXyzN qn[4];
    for (int i = 0; i < 4; i++) qn[i] = { qx[i].x, qx[i].y, qx[i].z, 0, 0, -1, qx[i].u, qx[i].v };
    VRhw qr[4] = { { x0 - .5f, y0 - .5f, .5f, 1, 0, 0 }, { x1 - .5f, y0 - .5f, .5f, 1, 1, 0 },
                   { x0 - .5f, y1 - .5f, .5f, 1, 0, 1 }, { x1 - .5f, y1 - .5f, .5f, 1, 1, 1 } };
    VRhwD qd[4];
    for (int i = 0; i < 4; i++) qd[i] = { qr[i].x, qr[i].y, qr[i].z, 1, 0xffffffff, qr[i].u, qr[i].v };

    IDirect3DVertexShader9 *vs = NULL;
    IDirect3DPixelShader9 *ps = NULL;
    const bool hlsl = !strncmp(c.path, "hlsl", 4);
    const bool sm3 = !strcmp(c.path, "hlsl3");
    if (hlsl) {
        IBlob *v = compile(vs_src, sm3 ? "vs_3_0" : "vs_2_0");
        IBlob *p = compile(ps_src, sm3 ? "ps_3_0" : "ps_2_0");
        if (v) { CHECK(dev->CreateVertexShader((const DWORD *)v->GetBufferPointer(), &vs), "CreateVertexShader"); v->Release(); }
        if (p) { CHECK(dev->CreatePixelShader((const DWORD *)p->GetBufferPointer(), &ps), "CreatePixelShader"); p->Release(); }
        if (!vs || !ps) strcat(err, " no-shaders");
    }

    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, zfmt != D3DFMT_UNKNOWN ? D3DZB_TRUE : D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_LIGHTING, !strcmp(c.path, "ff-xyz-lit"));
    dev->SetTexture(0, tex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, !strcmp(c.mip, "none") ? D3DTEXF_NONE :
                         !strcmp(c.mip, "point") ? D3DTEXF_POINT : D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, (DWORD)c.maxmip);
    float bias = c.bias;
    dev->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, *(DWORD *)&bias);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    if (!hlsl) {
        bool mod = !strcmp(c.path, "ff-rhw-diffuse") || !strcmp(c.path, "ff-rhw-mod") ||
                   !strcmp(c.path, "ff-xyz-lit");
        dev->SetTextureStageState(0, D3DTSS_COLOROP, mod ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        D3DMATRIX id = {};
        id._11 = id._22 = id._33 = id._44 = 1.0f;
        dev->SetTransform(D3DTS_WORLD, &id);
        dev->SetTransform(D3DTS_VIEW, &id);
        dev->SetTransform(D3DTS_PROJECTION, &id);
        if (!strcmp(c.path, "ff-xyz-lit")) {
            D3DLIGHT9 light = {};
            light.Type = D3DLIGHT_DIRECTIONAL;
            light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1.0f;
            light.Direction.z = 1.0f;
            dev->SetLight(0, &light);
            dev->LightEnable(0, TRUE);
            D3DMATERIAL9 mat = {};
            mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.0f;
            dev->SetMaterial(&mat);
            dev->SetRenderState(D3DRS_AMBIENT, 0);
        }
    }

    IDirect3DSurface9 *rt = NULL, *sys = NULL;
    CHECK(dev->GetRenderTarget(0, &rt), "GetRenderTarget");
    CHECK(dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL),
          "CreateOffscreenPlainSurface");

    DWORD centre = 0, corner = 0;
    int got = -1, dist = 0, want = expected_level(c, nlevels > 0 ? nlevels : 1);
    for (int f = 0; f <= c.frames; f++) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        dev->Clear(0, NULL, D3DCLEAR_TARGET | (zfmt != D3DFMT_UNKNOWN ? D3DCLEAR_ZBUFFER : 0),
                   D3DCOLOR_XRGB(20, 20, 40), 1.0f, 0);
        dev->BeginScene();
        HRESULT dh;
        if (hlsl) {
            dev->SetVertexShader(vs);
            dev->SetPixelShader(ps);
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
            dh = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qx, sizeof(VXyz));
        } else if (!strcmp(c.path, "ff-rhw") || !strcmp(c.path, "ff-rhw-mod")) {
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            dh = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qr, sizeof(VRhw));
        } else if (!strcmp(c.path, "ff-rhw-diffuse")) {
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            dh = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qd, sizeof(VRhwD));
        } else if (!strcmp(c.path, "ff-xyz-lit")) {
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
            dh = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qn, sizeof(VXyzN));
        } else {
            dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
            dh = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qx, sizeof(VXyz));
        }
        if (f == 0) CHECK(dh, "Draw");
        dev->EndScene();
        if (f == 0 && rt && sys) {
            CHECK(dev->GetRenderTargetData(rt, sys), "GetRenderTargetData");
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(sys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
                centre = ((DWORD *)((BYTE *)lr.pBits + lr.Pitch * (H / 2)))[W / 2] & 0xffffff;
                corner = ((DWORD *)((BYTE *)lr.pBits + lr.Pitch * (int)(y0 + 2)))[(int)(x0 + 2)] & 0xffffff;
                sys->UnlockRect();
            }
            got = nearest_level(centre, &dist);
        }
        HRESULT ph = dev->Present(NULL, NULL, NULL, NULL);
        if (f == 0) CHECK(ph, "Present");
    }

    /* Linear mip filtering blends two levels; accept a neighbour then. */
    bool near_ok = dist < 40 * 40 * 3;
    bool level_ok = got == want || (!strcmp(c.mip, "linear") && abs(got - want) <= 1 && dist < 128 * 128);
    bool pass = !err[0] && (near_ok || !strcmp(c.mip, "linear")) && level_ok;
    logf("%s %s levels_created=%d centre=%06lx corner=%06lx matches_level=%d want_level=%d%s%s\n",
         pass ? "PASS" : "FAIL", desc, nlevels, (unsigned long)centre, (unsigned long)corner, got, want,
         err[0] ? " errors:" : "", err);

    if (sys) sys->Release();
    if (rt) rt->Release();
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (stage) stage->Release();
    if (tex) tex->Release();
    dev->Release();
    return pass;
}

static const char *matrix[] = {
    /* The BasicHLSL texture: X8R8G8B8, 9 levels, MANAGED, linear mips. */
    "path=hlsl",
    "path=hlsl size=256",
    "path=hlsl levels=1",
    "path=hlsl fmt=a8",
    "path=hlsl fmt=dxt1",
    "path=hlsl fmt=r5g6b5",
    "path=hlsl fmt=r5g6b5 levels=1",
    "path=hlsl fmt=a1r5g5b5",
    "path=hlsl pool=default",
    "path=hlsl pool=dynamic",
    "path=hlsl mip=point",
    "path=hlsl mip=none",
    "path=hlsl mip=point maxmip=3",
    "path=hlsl mip=point bias=1",
    "path=hlsl3",
    "path=hlsl depth=d24x8",
    "path=hlsl depth=d24x8 levels=1",
    "path=hlsl depth=d24x8 fmt=a8",
    "path=ff-rhw",
    "path=ff-rhw levels=1",
    "path=ff-rhw-diffuse",
    "path=ff-rhw-diffuse levels=1",
    "path=ff-rhw-mod",
    "path=ff-rhw-diffuse depth=d24x8",
    "path=ff-xyz",
    "path=ff-xyz-lit",
    "path=ff-xyz depth=d24x8",
};

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    *wcsrchr(path, L'\\') = 0;
    wchar_t logpath[MAX_PATH];
    swprintf(logpath, MAX_PATH, L"%ls\\d3d9_tex.txt", path);
    logf_ = _wfopen(logpath, L"a");
    if (!logf_) return 200;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = L"d3d9_tex";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r = { 0, 0, W, H };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"d3d9_tex", L"d3d9_tex", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              40, 40, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { logf("FAIL Direct3DCreate9\n"); return 201; }
    D3DADAPTER_IDENTIFIER9 id = {};
    d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    logf("== d3d9_tex %d-bit, adapter %s\n", (int)sizeof(void *) * 8, id.Description);

    Config base;
    bool use_matrix = false;
    for (int i = 1; i < argc; i++) {
        char a[128];
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, a, sizeof a, NULL, NULL);
        if (!strcmp(a, "matrix")) use_matrix = true;
        else if (!set_key(base, a)) logf("bad argument %s\n", a);
    }
    int fails = 0, total = 0;
    if (use_matrix) {
        for (size_t m = 0; m < sizeof matrix / sizeof *matrix; m++) {
            Config c = base;
            char buf[256];
            snprintf(buf, sizeof buf, "%s", matrix[m]);
            for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) set_key(c, t);
            total++;
            if (!run_case(d3d, hwnd, c)) fails++;
        }
    } else {
        total = 1;
        if (!run_case(d3d, hwnd, base)) fails++;
    }
    logf("== done %d of %d passed\n", total - fails, total);
    d3d->Release();
    fclose(logf_);
    return fails;
}
