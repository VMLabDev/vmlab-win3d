/* ffp: D3D9 fixed-function pipeline coverage. Each case renders into a
 * 64x64 A8R8G8B8 render target on its own device, reads it back with
 * GetRenderTargetData and logs an 8x8 grid of pixels (at 4 + 8i), so the
 * same run on the reference rasterizer gives the expected values.
 *
 *   d3d9_ffp.exe [--ref] [--swvp] [--log FILE] [--only SUBSTR]...
 *
 *   --ref   D3DDEVTYPE_REF (d3dref9.dll from the DirectX SDK in the system
 *           directory) instead of D3DDEVTYPE_HAL
 *   --swvp  D3DCREATE_SOFTWARE_VERTEXPROCESSING (default: hardware)
 *   --only  run the cases whose name contains SUBSTR (repeatable)
 *
 * Log lines:
 *   CASE <name> tol=<n> hr=<first failed HRESULT or 0> px=<64 x AARRGGBB>
 *   RESULT ran=<n> failed_hr=<n>
 * Geometry edges sit on multiples of 8, half-way between sample points, so
 * rasterization rules do not move a sample from one primitive to another.
 * Compare two logs with compare.py. Exit code: number of cases with an
 * HRESULT failure.
 */
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define W 64
#define H 64
#define GRID 8

static FILE *logf_;
static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf_, fmt, ap);
    va_end(ap);
    fflush(logf_);
}

/* ---- state shared by the cases ---------------------------------------- */

struct Ctx {
    IDirect3DDevice9 *dev;
    HRESULT hr; /* first failure */
    IDirect3DTexture9 *t0, *t1, *grad, *agrad, *nmap, *white;
};

static void check(Ctx &c, HRESULT hr, const char *what)
{
    if (FAILED(hr) && SUCCEEDED(c.hr)) {
        c.hr = hr;
        logf("  fail %s hr=0x%08lx\n", what, (unsigned long)hr);
    }
}
#define CK(expr) check(c, (expr), #expr)

static DWORD fbits(float f) { DWORD d; memcpy(&d, &f, 4); return d; }

/* Constant colours of the blending cases. */
static const DWORD DIFF = 0x99cc6633, SPEC = 0x40204060, TF = 0x80408cc0;
static const DWORD T0C = 0xb0c08040, T1C = 0x6030a0e0;

static IDirect3DTexture9 *make_tex(Ctx &c, UINT w, UINT h, DWORD (*f)(UINT, UINT, UINT, UINT, DWORD),
                                   DWORD arg)
{
    IDirect3DTexture9 *t = NULL;
    CK(c.dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL));
    if (!t) return NULL;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(t->LockRect(0, &lr, NULL, 0))) {
        for (UINT y = 0; y < h; y++) {
            DWORD *row = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
            for (UINT x = 0; x < w; x++) row[x] = f(x, y, w, h, arg);
        }
        t->UnlockRect(0);
    }
    return t;
}
static DWORD f_solid(UINT, UINT, UINT, UINT, DWORD a) { return a; }
/* Red = u, green = v: with point sampling the colour reads back the
 * texture coordinate (256x256, one texel per step). */
static DWORD f_grad(UINT x, UINT y, UINT, UINT, DWORD) { return 0xff000000 | (x << 16) | (y << 8) | 0x40; }
/* White, alpha = u. */
static DWORD f_agrad(UINT x, UINT, UINT w, UINT, DWORD) { return ((x * 255 / (w - 1)) << 24) | 0xffffff; }

/* ---- vertices --------------------------------------------------------- */

struct V {
    float p[4];
    float n[3];
    float psize;
    DWORD diff, spec;
    float tc[4][4];
    float weight;
};

static V vtx(float x, float y, float z, float w = 1.0f)
{
    V v;
    memset(&v, 0, sizeof v);
    v.p[0] = x; v.p[1] = y; v.p[2] = z; v.p[3] = w;
    v.n[2] = -1.0f;
    v.diff = DIFF;
    v.spec = SPEC;
    return v;
}

static int fvf_tc_size(DWORD fvf, int i)
{
    switch ((fvf >> (i * 2 + 16)) & 3) {
    case D3DFVF_TEXTUREFORMAT1: return 1;
    case D3DFVF_TEXTUREFORMAT3: return 3;
    case D3DFVF_TEXTUREFORMAT4: return 4;
    default: return 2;
    }
}

static void pack(DWORD fvf, const V *in, int n, std::vector<float> &out, UINT *stride)
{
    out.clear();
    for (int k = 0; k < n; k++) {
        const V &v = in[k];
        size_t start = out.size();
        DWORD pos = fvf & D3DFVF_POSITION_MASK;
        out.push_back(v.p[0]); out.push_back(v.p[1]); out.push_back(v.p[2]);
        if (pos == D3DFVF_XYZRHW) out.push_back(v.p[3]);
        if (pos == D3DFVF_XYZB1) out.push_back(v.weight);
        if (fvf & D3DFVF_NORMAL) { out.push_back(v.n[0]); out.push_back(v.n[1]); out.push_back(v.n[2]); }
        if (fvf & D3DFVF_PSIZE) out.push_back(v.psize);
        float f;
        if (fvf & D3DFVF_DIFFUSE) { memcpy(&f, &v.diff, 4); out.push_back(f); }
        if (fvf & D3DFVF_SPECULAR) { memcpy(&f, &v.spec, 4); out.push_back(f); }
        int ntc = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
        for (int i = 0; i < ntc; i++)
            for (int j = 0; j < fvf_tc_size(fvf, i); j++) out.push_back(v.tc[i][j]);
        *stride = (UINT)((out.size() - start) * 4);
    }
}

static void draw(Ctx &c, D3DPRIMITIVETYPE prim, DWORD fvf, const V *v, int n)
{
    std::vector<float> buf;
    UINT stride;
    pack(fvf, v, n, buf, &stride);
    CK(c.dev->SetFVF(fvf));
    UINT count = prim == D3DPT_TRIANGLESTRIP ? n - 2 : prim == D3DPT_TRIANGLELIST ? n / 3 :
                 prim == D3DPT_POINTLIST ? n : prim == D3DPT_LINELIST ? n / 2 : n - 1;
    CK(c.dev->DrawPrimitiveUP(prim, count, buf.data(), stride));
}

/* A strip of four corners: top left, top right, bottom left, bottom right.
 * uv runs 0..1 over the quad in texcoord set 0 and 1. */
static void corners_uv(V q[4])
{
    for (int i = 0; i < 4; i++) {
        float u = (float)(i & 1), v = (float)(i >> 1);
        q[i].tc[0][0] = q[i].tc[1][0] = u;
        q[i].tc[0][1] = q[i].tc[1][1] = v;
    }
}

/* Pre-transformed quad in pixels. */
static void quad_rhw(V q[4], float x0, float y0, float x1, float y1, float z = 0.5f)
{
    q[0] = vtx(x0, y0, z); q[1] = vtx(x1, y0, z); q[2] = vtx(x0, y1, z); q[3] = vtx(x1, y1, z);
    corners_uv(q);
}

/* Untransformed quad in object space (identity transforms: clip space). */
static void quad_xyz(V q[4], float x0, float y0, float x1, float y1, float z = 0.5f)
{
    q[0] = vtx(x0, y0, z); q[1] = vtx(x1, y0, z); q[2] = vtx(x0, y1, z); q[3] = vtx(x1, y1, z);
    corners_uv(q);
}

/* ---- matrices --------------------------------------------------------- */

static D3DMATRIX ident()
{
    D3DMATRIX m;
    memset(&m, 0, sizeof m);
    m._11 = m._22 = m._33 = m._44 = 1.0f;
    return m;
}
static D3DMATRIX scale_tr(float sx, float sy, float sz, float tx, float ty, float tz)
{
    D3DMATRIX m = ident();
    m._11 = sx; m._22 = sy; m._33 = sz;
    m._41 = tx; m._42 = ty; m._43 = tz;
    return m;
}
/* Left-handed perspective, as D3DXMatrixPerspectiveFovLH. */
static D3DMATRIX persp(float fovy, float aspect, float zn, float zf)
{
    D3DMATRIX m;
    memset(&m, 0, sizeof m);
    float ys = 1.0f / tanf(fovy / 2), xs = ys / aspect;
    m._11 = xs; m._22 = ys;
    m._33 = zf / (zf - zn); m._34 = 1.0f;
    m._43 = -zn * zf / (zf - zn);
    return m;
}
static D3DMATRIX rot_y(float a)
{
    D3DMATRIX m = ident();
    m._11 = cosf(a); m._13 = -sinf(a);
    m._31 = sinf(a); m._33 = cosf(a);
    return m;
}

static void set_xf(Ctx &c, const D3DMATRIX &world, const D3DMATRIX &view, const D3DMATRIX &proj)
{
    CK(c.dev->SetTransform(D3DTS_WORLD, &world));
    CK(c.dev->SetTransform(D3DTS_VIEW, &view));
    CK(c.dev->SetTransform(D3DTS_PROJECTION, &proj));
}

/* ---- texture stages --------------------------------------------------- */

static void cop(Ctx &c, int s, DWORD op, DWORD a1 = D3DTA_TEXTURE, DWORD a2 = D3DTA_CURRENT)
{
    CK(c.dev->SetTextureStageState(s, D3DTSS_COLOROP, op));
    CK(c.dev->SetTextureStageState(s, D3DTSS_COLORARG1, a1));
    CK(c.dev->SetTextureStageState(s, D3DTSS_COLORARG2, a2));
}
static void aop(Ctx &c, int s, DWORD op, DWORD a1 = D3DTA_TEXTURE, DWORD a2 = D3DTA_CURRENT)
{
    CK(c.dev->SetTextureStageState(s, D3DTSS_ALPHAOP, op));
    CK(c.dev->SetTextureStageState(s, D3DTSS_ALPHAARG1, a1));
    CK(c.dev->SetTextureStageState(s, D3DTSS_ALPHAARG2, a2));
}
static void tss(Ctx &c, int s, D3DTEXTURESTAGESTATETYPE t, DWORD v) { CK(c.dev->SetTextureStageState(s, t, v)); }
static void rs(Ctx &c, D3DRENDERSTATETYPE t, DWORD v) { CK(c.dev->SetRenderState(t, v)); }
static void tex(Ctx &c, int s, IDirect3DTexture9 *t) { CK(c.dev->SetTexture(s, t)); }
static void point_filter(Ctx &c, int s)
{
    CK(c.dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT));
    CK(c.dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
    CK(c.dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
    CK(c.dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
    CK(c.dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
}

/* ---- blending cases: one quad, solid textures, both vertex paths ------ */

enum Path { RHW, XYZ };
static Path g_path;

static void blend_quad(Ctx &c, int ntex)
{
    V q[4];
    DWORD fvf = D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2;
    if (g_path == RHW) {
        quad_rhw(q, 0, 0, W, H);
        fvf |= D3DFVF_XYZRHW;
    } else {
        quad_xyz(q, -1, 1, 1, -1);
        fvf |= D3DFVF_XYZ;
    }
    (void)ntex;
    CK(c.dev->SetRenderState(D3DRS_TEXTUREFACTOR, TF));
    draw(c, D3DPT_TRIANGLESTRIP, fvf, q, 4);
}

static void bind01(Ctx &c) { tex(c, 0, c.t0); tex(c, 1, c.t1); }

#define BLEND(name, body) static void name(Ctx &c) { bind01(c); body; blend_quad(c, 2); }

BLEND(b_sel_tex, cop(c, 0, D3DTOP_SELECTARG1); aop(c, 0, D3DTOP_SELECTARG1))
BLEND(b_sel_diff, cop(c, 0, D3DTOP_SELECTARG2, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG2, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_sel_tf, cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TFACTOR); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TFACTOR))
BLEND(b_sel_spec, cop(c, 0, D3DTOP_SELECTARG1, D3DTA_SPECULAR); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_SPECULAR))
BLEND(b_mod, cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_mod2x, cop(c, 0, D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_mod4x, cop(c, 0, D3DTOP_MODULATE4X, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_MODULATE4X, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_add, cop(c, 0, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_addsigned, cop(c, 0, D3DTOP_ADDSIGNED, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_ADDSIGNED, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_addsigned2x, cop(c, 0, D3DTOP_ADDSIGNED2X, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_ADDSIGNED2X, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_subtract, cop(c, 0, D3DTOP_SUBTRACT, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SUBTRACT, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_addsmooth, cop(c, 0, D3DTOP_ADDSMOOTH, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_ADDSMOOTH, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_blendtexalpha, cop(c, 0, D3DTOP_BLENDTEXTUREALPHA, D3DTA_TFACTOR, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE))
BLEND(b_blenddiffalpha, cop(c, 0, D3DTOP_BLENDDIFFUSEALPHA, D3DTA_TEXTURE, D3DTA_TFACTOR); aop(c, 0, D3DTOP_BLENDDIFFUSEALPHA, D3DTA_TEXTURE, D3DTA_TFACTOR))
BLEND(b_blendfactoralpha, cop(c, 0, D3DTOP_BLENDFACTORALPHA, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE))
BLEND(b_blendcurrentalpha,
      cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
      cop(c, 1, D3DTOP_BLENDCURRENTALPHA, D3DTA_TEXTURE, D3DTA_TFACTOR); aop(c, 1, D3DTOP_SELECTARG1, D3DTA_CURRENT))
BLEND(b_blendtexalphapm, cop(c, 0, D3DTOP_BLENDTEXTUREALPHAPM, D3DTA_TFACTOR, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE))
BLEND(b_modalpha_addcolor, cop(c, 0, D3DTOP_MODULATEALPHA_ADDCOLOR, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE))
BLEND(b_modcolor_addalpha, cop(c, 0, D3DTOP_MODULATECOLOR_ADDALPHA, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE))
BLEND(b_dot3, tex(c, 0, c.nmap); cop(c, 0, D3DTOP_DOTPRODUCT3, D3DTA_TEXTURE, D3DTA_TFACTOR); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE))
BLEND(b_dot3_alpha, tex(c, 0, c.nmap); cop(c, 0, D3DTOP_DOTPRODUCT3, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_DOTPRODUCT3, D3DTA_TEXTURE, D3DTA_DIFFUSE))
BLEND(b_lerp, cop(c, 0, D3DTOP_LERP, D3DTA_TEXTURE, D3DTA_DIFFUSE); tss(c, 0, D3DTSS_COLORARG0, D3DTA_TFACTOR);
      aop(c, 0, D3DTOP_LERP, D3DTA_TEXTURE, D3DTA_DIFFUSE); tss(c, 0, D3DTSS_ALPHAARG0, D3DTA_SPECULAR))
BLEND(b_multiplyadd, cop(c, 0, D3DTOP_MULTIPLYADD, D3DTA_TEXTURE, D3DTA_DIFFUSE); tss(c, 0, D3DTSS_COLORARG0, D3DTA_SPECULAR);
      aop(c, 0, D3DTOP_MULTIPLYADD, D3DTA_TEXTURE, D3DTA_DIFFUSE); tss(c, 0, D3DTSS_ALPHAARG0, D3DTA_TFACTOR))
BLEND(b_complement, cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE | D3DTA_COMPLEMENT, D3DTA_DIFFUSE);
      aop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE | D3DTA_COMPLEMENT))
BLEND(b_alpharep, cop(c, 0, D3DTOP_MODULATE, D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE, D3DTA_TEXTURE);
      aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TFACTOR))
BLEND(b_comp_alpharep, cop(c, 0, D3DTOP_ADD, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE | D3DTA_COMPLEMENT, D3DTA_SPECULAR);
      aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE | D3DTA_COMPLEMENT))
BLEND(b_2stage_add,
      cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
      cop(c, 1, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT); aop(c, 1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT))
BLEND(b_2stage_lightmap,
      cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
      cop(c, 1, D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_CURRENT); aop(c, 1, D3DTOP_SELECTARG2, D3DTA_TEXTURE, D3DTA_CURRENT);
      tss(c, 1, D3DTSS_TEXCOORDINDEX, 1))
BLEND(b_3stage,
      cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
      cop(c, 1, D3DTOP_BLENDFACTORALPHA, D3DTA_TEXTURE, D3DTA_CURRENT); aop(c, 1, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT);
      cop(c, 2, D3DTOP_ADD, D3DTA_CURRENT, D3DTA_SPECULAR); aop(c, 2, D3DTOP_SELECTARG1, D3DTA_CURRENT))
BLEND(b_4stage,
      tex(c, 2, c.t0); tex(c, 3, c.t1);
      cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
      cop(c, 1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT); aop(c, 1, D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_CURRENT);
      cop(c, 2, D3DTOP_ADDSIGNED, D3DTA_CURRENT, D3DTA_TFACTOR); aop(c, 2, D3DTOP_ADD, D3DTA_CURRENT, D3DTA_DIFFUSE);
      cop(c, 3, D3DTOP_SUBTRACT, D3DTA_CURRENT, D3DTA_SPECULAR); aop(c, 3, D3DTOP_SUBTRACT, D3DTA_CURRENT, D3DTA_SPECULAR);
      tss(c, 2, D3DTSS_TEXCOORDINDEX, 0); tss(c, 3, D3DTSS_TEXCOORDINDEX, 1))
BLEND(b_disable_stage1,
      cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
      cop(c, 1, D3DTOP_DISABLE); cop(c, 2, D3DTOP_ADD, D3DTA_TFACTOR, D3DTA_CURRENT))
BLEND(b_temp,
      cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
      tss(c, 0, D3DTSS_RESULTARG, D3DTA_TEMP);
      cop(c, 1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 1, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
      cop(c, 2, D3DTOP_ADD, D3DTA_TEMP, D3DTA_CURRENT); aop(c, 2, D3DTOP_MODULATE, D3DTA_TEMP, D3DTA_CURRENT))
BLEND(b_specularenable,
      cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE); aop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
      rs(c, D3DRS_SPECULARENABLE, TRUE))
BLEND(b_constant,
      tss(c, 0, D3DTSS_CONSTANT, 0x7f20c0ff);
      cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CONSTANT); aop(c, 0, D3DTOP_SELECTARG1, D3DTA_CONSTANT))
/* Texture and diffuse defaults: no texture bound and no diffuse in the
 * vertex (D3DFVF without DIFFUSE). */
static void b_defaults(Ctx &c)
{
    V q[4];
    DWORD fvf = D3DFVF_TEX1;
    if (g_path == RHW) { quad_rhw(q, 0, 0, W, H); fvf |= D3DFVF_XYZRHW; }
    else { quad_xyz(q, -1, 1, 1, -1); fvf |= D3DFVF_XYZ; }
    cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
    draw(c, D3DPT_TRIANGLESTRIP, fvf, q, 4);
}

/* ---- texture coordinates: the gradient texture reads them back -------- */

/* A quad over the whole target with per-vertex normals that lean out from
 * the centre, untransformed, identity transforms. */
static void tc_quad(Ctx &c, DWORD fvf, void (*edit)(V *) = NULL)
{
    V q[4];
    quad_xyz(q, -1, 1, 1, -1, 0.5f);
    for (int i = 0; i < 4; i++) {
        float nx = (i & 1) ? 0.6f : -0.6f, ny = (i >> 1) ? -0.6f : 0.6f;
        q[i].n[0] = nx; q[i].n[1] = ny; q[i].n[2] = -0.52915f;
        if (edit) edit(&q[i]);
    }
    tex(c, 0, c.grad);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    draw(c, D3DPT_TRIANGLESTRIP, fvf, q, 4);
}

static void tg_identity(Ctx &c) { tc_quad(c, D3DFVF_XYZ | D3DFVF_TEX1); }
static void tg_rhw_identity(Ctx &c)
{
    V q[4];
    quad_rhw(q, 0, 0, W, H);
    tex(c, 0, c.grad); point_filter(c, 0);
    cop(c, 0, D3DTOP_SELECTARG1); aop(c, 0, D3DTOP_SELECTARG1);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_TEX1, q, 4);
}
static void tg_index1(Ctx &c)
{
    tss(c, 0, D3DTSS_TEXCOORDINDEX, 1);
    tc_quad(c, D3DFVF_XYZ | D3DFVF_TEX2, [](V *v) { v->tc[1][0] = 1.0f - v->tc[0][0]; v->tc[0][0] = 0.0f; });
}
static void tg_count2(Ctx &c)
{
    D3DMATRIX m = ident();
    m._11 = 0.5f; m._22 = 0.75f; m._31 = 0.25f; m._32 = 0.125f;
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    tc_quad(c, D3DFVF_XYZ | D3DFVF_TEX1);
}
static void tg_count2_rhw(Ctx &c)
{
    D3DMATRIX m = ident();
    m._11 = 0.5f; m._22 = 0.75f; m._31 = 0.25f; m._32 = 0.125f;
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    tg_rhw_identity(c);
}
static void tg_count3_projected(Ctx &c)
{
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
    D3DMATRIX m = ident();
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tc_quad(c, D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0), [](V *v) {
        float q = 1.0f + v->tc[0][0] * 1.5f + v->tc[0][1] * 0.5f;
        v->tc[0][0] *= q * 0.8f; v->tc[0][1] *= q * 0.6f; v->tc[0][2] = q;
    });
}
static void tg_count4_projected(Ctx &c)
{
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT4 | D3DTTFF_PROJECTED);
    D3DMATRIX m = ident();
    m._34 = 0.0f;
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tc_quad(c, D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE4(0), [](V *v) {
        float q = 2.0f - v->tc[0][0];
        v->tc[0][0] *= q * 0.9f; v->tc[0][1] *= q * 0.9f; v->tc[0][2] = 0.0f; v->tc[0][3] = q;
    });
}
/* Camera-space vectors in -1..1 mapped to 0..1 by the texture matrix. The
 * generated vector is (x, y, z, 1), so the offset sits in the fourth row. */
static void texgen(Ctx &c, DWORD tci)
{
    D3DMATRIX m = ident();
    m._11 = 0.5f; m._22 = -0.5f; m._41 = 0.5f; m._42 = 0.5f;
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    tss(c, 0, D3DTSS_TEXCOORDINDEX, tci);
}
static void tg_campos(Ctx &c) { texgen(c, D3DTSS_TCI_CAMERASPACEPOSITION); tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1); }
static void tg_campos_world(Ctx &c)
{
    D3DMATRIX w = scale_tr(0.5f, 0.5f, 1, 0.25f, -0.25f, 0), v = scale_tr(1, 1, 1, 0, 0, 1.0f);
    D3DMATRIX p = ident();
    p._43 = -1.0f;
    set_xf(c, w, v, p);
    texgen(c, D3DTSS_TCI_CAMERASPACEPOSITION);
    tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
}
static void tg_camnormal(Ctx &c) { texgen(c, D3DTSS_TCI_CAMERASPACENORMAL); tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1); }
static void tg_camnormal_rot(Ctx &c)
{
    D3DMATRIX w = rot_y(0.5f);
    set_xf(c, w, ident(), ident());
    texgen(c, D3DTSS_TCI_CAMERASPACENORMAL);
    tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
}
static void tg_camreflect(Ctx &c) { texgen(c, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR); tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1); }
static void tg_camreflect_nolocal(Ctx &c)
{
    rs(c, D3DRS_LOCALVIEWER, FALSE);
    texgen(c, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
    tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
}
static void tg_sphere(Ctx &c)
{
    tss(c, 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_SPHEREMAP);
    tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
}
static void tg_count3_cube(Ctx &c)
{
    /* Generated reflection vector through a 3-component transform; the
     * gradient is a 2D texture, so only the first two components sample. */
    D3DMATRIX m = ident();
    m._11 = 0.5f; m._22 = 0.5f; m._41 = 0.5f; m._42 = 0.5f;
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
    tss(c, 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACENORMAL);
    tc_quad(c, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
}

/* ---- fog ------------------------------------------------------------- */

static const DWORD FOGC = 0xff20ff80;

/* Quad whose depth runs from z0 at the left to z1 at the right. */
static void fog_quad(Ctx &c, float z0, float z1, float x0 = -1, float x1 = 1, bool textured = false)
{
    V q[4];
    quad_xyz(q, x0, 1, x1, -1);
    q[0].p[2] = q[2].p[2] = z0;
    q[1].p[2] = q[3].p[2] = z1;
    for (int i = 0; i < 4; i++) q[i].diff = 0xffc03020;
    if (textured) {
        tex(c, 0, c.t0);
        cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
    } else {
        cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    }
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    rs(c, D3DRS_FOGENABLE, TRUE);
    rs(c, D3DRS_FOGCOLOR, FOGC);
    rs(c, D3DRS_FOGSTART, fbits(0.2f));
    rs(c, D3DRS_FOGEND, fbits(0.8f));
    rs(c, D3DRS_FOGDENSITY, fbits(2.0f));
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, q, 4);
}
static void fog_vtx(Ctx &c, DWORD mode) { rs(c, D3DRS_FOGVERTEXMODE, mode); }
static void fog_tbl(Ctx &c, DWORD mode) { rs(c, D3DRS_FOGTABLEMODE, mode); }
static void fv_linear(Ctx &c) { fog_vtx(c, D3DFOG_LINEAR); fog_quad(c, 0.0f, 1.0f); }
static void fv_exp(Ctx &c) { fog_vtx(c, D3DFOG_EXP); fog_quad(c, 0.0f, 1.0f); }
static void fv_exp2(Ctx &c) { fog_vtx(c, D3DFOG_EXP2); fog_quad(c, 0.0f, 1.0f); }
static void ft_linear(Ctx &c) { fog_tbl(c, D3DFOG_LINEAR); fog_quad(c, 0.0f, 1.0f); }
static void ft_exp(Ctx &c) { fog_tbl(c, D3DFOG_EXP); fog_quad(c, 0.0f, 1.0f); }
static void ft_exp2(Ctx &c) { fog_tbl(c, D3DFOG_EXP2); fog_quad(c, 0.0f, 1.0f); }
static void fv_linear_tex(Ctx &c) { fog_vtx(c, D3DFOG_LINEAR); fog_quad(c, 0.0f, 1.0f, -1, 1, true); }
static void fv_range(Ctx &c)
{
    fog_vtx(c, D3DFOG_LINEAR);
    rs(c, D3DRS_RANGEFOGENABLE, TRUE);
    /* View offset in x, so the range differs from z. */
    set_xf(c, ident(), scale_tr(1, 1, 1, 0.6f, 0, 0), ident());
    fog_quad(c, 0.0f, 0.6f, -1.6f, 0.4f);
}
/* Perspective: table fog in eye depth (w fog), vertex fog in eye z. */
static void fog_persp(Ctx &c)
{
    D3DMATRIX p = persp(1.2f, 1.0f, 1.0f, 20.0f);
    set_xf(c, ident(), ident(), p);
    V q[4];
    q[0] = vtx(-3, 3, 2); q[1] = vtx(12, 3, 14); q[2] = vtx(-3, -3, 2); q[3] = vtx(12, -3, 14);
    for (int i = 0; i < 4; i++) q[i].diff = 0xffc03020;
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    rs(c, D3DRS_FOGENABLE, TRUE);
    rs(c, D3DRS_FOGCOLOR, FOGC);
    rs(c, D3DRS_FOGSTART, fbits(3.0f));
    rs(c, D3DRS_FOGEND, fbits(12.0f));
    rs(c, D3DRS_FOGDENSITY, fbits(0.15f));
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
}
static void ft_linear_persp(Ctx &c) { fog_tbl(c, D3DFOG_LINEAR); fog_persp(c); }
static void ft_exp_persp(Ctx &c) { fog_tbl(c, D3DFOG_EXP); fog_persp(c); }
static void fv_linear_persp(Ctx &c) { fog_vtx(c, D3DFOG_LINEAR); fog_persp(c); }
static void fv_exp2_persp(Ctx &c) { fog_vtx(c, D3DFOG_EXP2); fog_persp(c); }
/* Pre-transformed: no vertex fog, the specular alpha is the fog factor. */
static void fog_rhw(Ctx &c, bool table, bool spec = true)
{
    V q[4];
    quad_rhw(q, 0, 0, W, H);
    q[0].p[2] = q[2].p[2] = 0.0f;
    q[1].p[2] = q[3].p[2] = 1.0f;
    for (int i = 0; i < 4; i++) {
        q[i].diff = 0xffc03020;
        q[i].spec = (i & 1) ? 0x00000000 : 0xff000000;
    }
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    rs(c, D3DRS_FOGENABLE, TRUE);
    rs(c, D3DRS_FOGCOLOR, FOGC);
    rs(c, D3DRS_FOGSTART, fbits(0.2f));
    rs(c, D3DRS_FOGEND, fbits(0.8f));
    if (table) rs(c, D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | (spec ? D3DFVF_SPECULAR : 0), q, 4);
}
static void fog_rhw_spec(Ctx &c) { fog_rhw(c, false); }
static void fog_rhw_nospec(Ctx &c) { fog_rhw(c, false, false); }
static void fog_rhw_table(Ctx &c) { fog_rhw(c, true); }

/* ---- lighting -------------------------------------------------------- */

/* An 8x8 grid of cells over the target; the normals lean out from the
 * centre like a dome, facing the viewer (-z). */
static void lit_grid(Ctx &c, DWORD fvf, void (*edit)(V *, float, float) = NULL)
{
    std::vector<V> tri;
    const int N = 8;
    auto at = [&](int i, int j) {
        float x = -1.0f + 2.0f * i / N, y = 1.0f - 2.0f * j / N;
        V v = vtx(x, y, 0.5f);
        float nx = x * 0.7f, ny = y * 0.7f, nz = -sqrtf(fmaxf(0.02f, 1.0f - nx * nx - ny * ny));
        v.n[0] = nx; v.n[1] = ny; v.n[2] = nz;
        v.tc[0][0] = (float)i / N; v.tc[0][1] = (float)j / N;
        /* Vertex colours for the COLORVERTEX cases. */
        v.diff = 0xff000000 | ((DWORD)(i * 255 / N) << 16) | ((DWORD)(j * 255 / N) << 8) | 0x80;
        v.spec = 0xff000000 | ((DWORD)((N - i) * 255 / N) << 8) | 0x30;
        if (edit) edit(&v, x, y);
        return v;
    };
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            V a = at(i, j), b = at(i + 1, j), cc = at(i, j + 1), d = at(i + 1, j + 1);
            tri.push_back(a); tri.push_back(b); tri.push_back(cc);
            tri.push_back(cc); tri.push_back(b); tri.push_back(d);
        }
    draw(c, D3DPT_TRIANGLELIST, fvf, tri.data(), (int)tri.size());
}

static D3DMATERIAL9 material()
{
    D3DMATERIAL9 m;
    memset(&m, 0, sizeof m);
    m.Diffuse = {0.8f, 0.6f, 0.4f, 0.7f};
    m.Ambient = {0.3f, 0.5f, 0.2f, 1.0f};
    m.Specular = {0.9f, 0.9f, 0.6f, 1.0f};
    m.Emissive = {0.05f, 0.0f, 0.1f, 0.0f};
    m.Power = 12.0f;
    return m;
}
static D3DLIGHT9 light(D3DLIGHTTYPE type)
{
    D3DLIGHT9 l;
    memset(&l, 0, sizeof l);
    l.Type = type;
    l.Diffuse = {1.0f, 0.9f, 0.8f, 1.0f};
    l.Specular = {1.0f, 1.0f, 1.0f, 1.0f};
    l.Ambient = {0.2f, 0.2f, 0.3f, 1.0f};
    l.Direction = {0.3f, -0.4f, 0.85f};
    l.Position = {0.4f, 0.3f, -0.4f};
    l.Range = 4.0f;
    l.Attenuation0 = 0.4f;
    l.Attenuation1 = 0.8f;
    l.Attenuation2 = 0.0f;
    l.Falloff = 1.0f;
    l.Theta = 0.4f;
    l.Phi = 1.0f;
    return l;
}
static void lit_setup(Ctx &c, const D3DLIGHT9 &l, int index = 0)
{
    D3DMATERIAL9 m = material();
    CK(c.dev->SetMaterial(&m));
    CK(c.dev->SetLight(index, &l));
    CK(c.dev->LightEnable(index, TRUE));
    rs(c, D3DRS_LIGHTING, TRUE);
    rs(c, D3DRS_AMBIENT, 0x00202830);
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
}
static const DWORD LITFVF = D3DFVF_XYZ | D3DFVF_NORMAL;
static void lt_dir(Ctx &c) { lit_setup(c, light(D3DLIGHT_DIRECTIONAL)); lit_grid(c, LITFVF); }
static void lt_dir_spec(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    rs(c, D3DRS_SPECULARENABLE, TRUE);
    lit_grid(c, LITFVF);
}
static void lt_dir_spec_nolocal(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    rs(c, D3DRS_SPECULARENABLE, TRUE);
    rs(c, D3DRS_LOCALVIEWER, FALSE);
    lit_grid(c, LITFVF);
}
static void lt_point(Ctx &c) { lit_setup(c, light(D3DLIGHT_POINT)); lit_grid(c, LITFVF); }
static void lt_point_spec(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_POINT));
    rs(c, D3DRS_SPECULARENABLE, TRUE);
    lit_grid(c, LITFVF);
}
static void lt_point_range(Ctx &c)
{
    D3DLIGHT9 l = light(D3DLIGHT_POINT);
    l.Range = 0.9f;
    l.Attenuation0 = 0.0f; l.Attenuation1 = 0.0f; l.Attenuation2 = 1.5f;
    lit_setup(c, l);
    lit_grid(c, LITFVF);
}
static void lt_spot(Ctx &c)
{
    D3DLIGHT9 l = light(D3DLIGHT_SPOT);
    l.Position = {0.1f, 0.0f, -0.6f};
    l.Direction = {0.0f, 0.1f, 1.0f};
    lit_setup(c, l);
    lit_grid(c, LITFVF);
}
static void lt_spot_falloff(Ctx &c)
{
    D3DLIGHT9 l = light(D3DLIGHT_SPOT);
    l.Position = {-0.2f, 0.2f, -0.8f};
    l.Direction = {0.2f, -0.2f, 1.0f};
    l.Theta = 0.2f; l.Phi = 0.9f; l.Falloff = 3.0f;
    l.Attenuation0 = 1.0f; l.Attenuation1 = 0.0f;
    lit_setup(c, l);
    lit_grid(c, LITFVF);
}
static void lt_three(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL), 0);
    D3DLIGHT9 p = light(D3DLIGHT_POINT);
    p.Diffuse = {0.2f, 0.4f, 1.0f, 1.0f};
    p.Position = {-0.5f, -0.5f, -0.3f};
    CK(c.dev->SetLight(1, &p));
    CK(c.dev->LightEnable(1, TRUE));
    D3DLIGHT9 s = light(D3DLIGHT_SPOT);
    s.Diffuse = {0.0f, 1.0f, 0.0f, 1.0f};
    s.Position = {0.5f, -0.5f, -0.5f};
    s.Direction = {0.0f, 0.0f, 1.0f};
    CK(c.dev->SetLight(2, &s));
    CK(c.dev->LightEnable(2, TRUE));
    rs(c, D3DRS_SPECULARENABLE, TRUE);
    lit_grid(c, LITFVF);
}
static void lt_ambient_only(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    CK(c.dev->LightEnable(0, FALSE));
    lit_grid(c, LITFVF);
}
/* Scaled world matrix: normals come out short without NORMALIZENORMALS. */
static void lt_scaled(Ctx &c, bool normalize)
{
    set_xf(c, scale_tr(0.5f, 0.5f, 0.5f, 0, 0, 0.25f), ident(), ident());
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    rs(c, D3DRS_NORMALIZENORMALS, normalize);
    lit_grid(c, LITFVF, [](V *v, float x, float y) { v->p[0] = x * 2; v->p[1] = y * 2; });
}
static void lt_scaled_nonorm(Ctx &c) { lt_scaled(c, false); }
static void lt_scaled_norm(Ctx &c) { lt_scaled(c, true); }
static void lt_colorvertex(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    rs(c, D3DRS_COLORVERTEX, TRUE);
    rs(c, D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    rs(c, D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    lit_grid(c, LITFVF | D3DFVF_DIFFUSE);
}
static void lt_colorvertex_off(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    rs(c, D3DRS_COLORVERTEX, FALSE);
    lit_grid(c, LITFVF | D3DFVF_DIFFUSE);
}
static void lt_colorvertex_spec_src(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_POINT));
    rs(c, D3DRS_SPECULARENABLE, TRUE);
    rs(c, D3DRS_COLORVERTEX, TRUE);
    rs(c, D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    rs(c, D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR1);
    rs(c, D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_COLOR2);
    lit_grid(c, LITFVF | D3DFVF_DIFFUSE | D3DFVF_SPECULAR);
}
static void lt_colorvertex_nodiffuse(Ctx &c)
{
    /* COLOR1 requested, but the vertex has no diffuse: the material is used. */
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    rs(c, D3DRS_COLORVERTEX, TRUE);
    rs(c, D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    lit_grid(c, LITFVF);
}
static void lt_tex_modulate(Ctx &c)
{
    lit_setup(c, light(D3DLIGHT_POINT));
    tex(c, 0, c.grad);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
    rs(c, D3DRS_SPECULARENABLE, TRUE);
    lit_grid(c, LITFVF | D3DFVF_TEX1);
}
static void lt_persp_point(Ctx &c)
{
    set_xf(c, ident(), scale_tr(1, 1, 1, 0, 0, 2.0f), persp(1.2f, 1.0f, 0.5f, 10.0f));
    D3DLIGHT9 l = light(D3DLIGHT_POINT);
    l.Position = {0.5f, 0.5f, 1.0f};
    l.Attenuation0 = 0.2f; l.Attenuation1 = 0.5f;
    lit_setup(c, l);
    rs(c, D3DRS_SPECULARENABLE, TRUE);
    lit_grid(c, LITFVF, [](V *v, float x, float y) { v->p[0] = x * 1.5f; v->p[1] = y * 1.5f; v->p[2] = 0.0f; });
}
static void lt_backface_cull_off(Ctx &c)
{
    /* Two-sided geometry: lighting is still for the stored normal. */
    rs(c, D3DRS_CULLMODE, D3DCULL_NONE);
    lit_setup(c, light(D3DLIGHT_DIRECTIONAL));
    lit_grid(c, LITFVF, [](V *v, float, float) { v->n[2] = -v->n[2]; });
}

/* ---- alpha test and blending ------------------------------------------ */

static void at_quad(Ctx &c, DWORD func, DWORD ref)
{
    V q[4];
    quad_rhw(q, 0, 0, W, H);
    tex(c, 0, c.agrad);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    rs(c, D3DRS_ALPHATESTENABLE, TRUE);
    rs(c, D3DRS_ALPHAFUNC, func);
    rs(c, D3DRS_ALPHAREF, ref);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, q, 4);
}
static void at_greater(Ctx &c) { at_quad(c, D3DCMP_GREATER, 0x80); }
static void at_greaterequal(Ctx &c) { at_quad(c, D3DCMP_GREATEREQUAL, 0xc0); }
static void at_less(Ctx &c) { at_quad(c, D3DCMP_LESS, 0x60); }
static void at_lessequal(Ctx &c) { at_quad(c, D3DCMP_LESSEQUAL, 0x24); }
static void at_notequal(Ctx &c) { at_quad(c, D3DCMP_NOTEQUAL, 0x00); }
static void at_never(Ctx &c) { at_quad(c, D3DCMP_NEVER, 0x00); }
static void at_xyz_diffuse(Ctx &c)
{
    V q[4];
    quad_xyz(q, -1, 1, 1, -1);
    for (int i = 0; i < 4; i++) q[i].diff = (i & 1) ? 0xff20c040 : 0x0020c040;
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    rs(c, D3DRS_ALPHATESTENABLE, TRUE);
    rs(c, D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    rs(c, D3DRS_ALPHAREF, 0x90);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
}

/* Blending: the target is cleared to CLEARC; the quad's colour and alpha
 * come from the texture factor, over the left 3/4 of the target with the
 * alpha gradient texture modulating alpha. */
static void ab_quad(Ctx &c, DWORD src, DWORD dst, DWORD op = D3DBLENDOP_ADD)
{
    V q[4];
    quad_rhw(q, 0, 0, W, H);
    tex(c, 0, c.agrad);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TFACTOR);
    aop(c, 0, D3DTOP_MODULATE, D3DTA_TFACTOR, D3DTA_TEXTURE);
    rs(c, D3DRS_TEXTUREFACTOR, 0xe0c04020);
    rs(c, D3DRS_ALPHABLENDENABLE, TRUE);
    rs(c, D3DRS_SRCBLEND, src);
    rs(c, D3DRS_DESTBLEND, dst);
    rs(c, D3DRS_BLENDOP, op);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_TEX1, q, 4);
}
static void ab_alpha(Ctx &c) { ab_quad(c, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA); }
static void ab_add(Ctx &c) { ab_quad(c, D3DBLEND_ONE, D3DBLEND_ONE); }
static void ab_srcalpha_one(Ctx &c) { ab_quad(c, D3DBLEND_SRCALPHA, D3DBLEND_ONE); }
static void ab_modulate(Ctx &c) { ab_quad(c, D3DBLEND_DESTCOLOR, D3DBLEND_ZERO); }
static void ab_modulate2x(Ctx &c) { ab_quad(c, D3DBLEND_DESTCOLOR, D3DBLEND_SRCCOLOR); }
static void ab_zero_srccolor(Ctx &c) { ab_quad(c, D3DBLEND_ZERO, D3DBLEND_SRCCOLOR); }
static void ab_invdestcolor(Ctx &c) { ab_quad(c, D3DBLEND_INVDESTCOLOR, D3DBLEND_ZERO); }
static void ab_destalpha(Ctx &c) { ab_quad(c, D3DBLEND_DESTALPHA, D3DBLEND_INVDESTALPHA); }
static void ab_premul(Ctx &c) { ab_quad(c, D3DBLEND_ONE, D3DBLEND_INVSRCALPHA); }
static void ab_bothsrcalpha(Ctx &c) { ab_quad(c, D3DBLEND_BOTHSRCALPHA, D3DBLEND_ZERO); }
static void ab_srcalphasat(Ctx &c) { ab_quad(c, D3DBLEND_SRCALPHASAT, D3DBLEND_ONE); }
static void ab_revsubtract(Ctx &c) { ab_quad(c, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT); }
static void ab_subtract(Ctx &c) { ab_quad(c, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_SUBTRACT); }
static void ab_min(Ctx &c) { ab_quad(c, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_MIN); }
static void ab_max(Ctx &c) { ab_quad(c, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_MAX); }
static void ab_blendfactor(Ctx &c)
{
    rs(c, D3DRS_BLENDFACTOR, 0x40c08020);
    ab_quad(c, D3DBLEND_BLENDFACTOR, D3DBLEND_INVBLENDFACTOR);
}
static void ab_separate_alpha(Ctx &c)
{
    rs(c, D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    rs(c, D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
    rs(c, D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
    ab_quad(c, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
}
static void ab_atest_and_blend(Ctx &c)
{
    rs(c, D3DRS_ALPHATESTENABLE, TRUE);
    rs(c, D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    rs(c, D3DRS_ALPHAREF, 0x50);
    ab_quad(c, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
}
static void ab_fog_blend(Ctx &c)
{
    rs(c, D3DRS_ALPHABLENDENABLE, TRUE);
    rs(c, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    rs(c, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    fog_vtx(c, D3DFOG_LINEAR);
    V q[4];
    quad_xyz(q, -1, 1, 1, -1);
    q[0].p[2] = q[2].p[2] = 0.0f;
    q[1].p[2] = q[3].p[2] = 1.0f;
    for (int i = 0; i < 4; i++) q[i].diff = 0x80c03020;
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    rs(c, D3DRS_FOGENABLE, TRUE);
    rs(c, D3DRS_FOGCOLOR, FOGC);
    rs(c, D3DRS_FOGSTART, fbits(0.2f));
    rs(c, D3DRS_FOGEND, fbits(0.8f));
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
}

/* ---- points ----------------------------------------------------------- */

/* Four points centred on the quadrants' centres (16, 48) with size 16,
 * so each covers 8..24 or 40..56. */
static void points(Ctx &c, bool rhw, DWORD extra_fvf = 0, void (*edit)(V *, int) = NULL)
{
    V p[4];
    for (int i = 0; i < 4; i++) {
        float px = (i & 1) ? 48.0f : 16.0f, py = (i >> 1) ? 48.0f : 16.0f;
        if (rhw) p[i] = vtx(px, py, 0.5f);
        else p[i] = vtx(px / 32.0f - 1.0f, 1.0f - py / 32.0f, 0.5f);
        p[i].diff = 0xff000000 | (0x40u << (8 * (i % 3))) | 0x101010;
        p[i].psize = 16.0f;
        if (edit) edit(&p[i], i);
    }
    draw(c, D3DPT_POINTLIST, (rhw ? D3DFVF_XYZRHW : D3DFVF_XYZ) | D3DFVF_DIFFUSE | extra_fvf, p, 4);
}
static void pt_setup(Ctx &c, bool sprite, bool textured)
{
    rs(c, D3DRS_POINTSIZE, fbits(16.0f));
    rs(c, D3DRS_POINTSPRITEENABLE, sprite);
    if (textured) {
        tex(c, 0, c.grad);
        point_filter(c, 0);
        cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    } else {
        cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    }
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
}
static void pt_size_rhw(Ctx &c) { pt_setup(c, false, false); points(c, true); }
static void pt_size_xyz(Ctx &c) { pt_setup(c, false, false); points(c, false); }
static void pt_sprite_rhw(Ctx &c) { pt_setup(c, true, true); points(c, true, D3DFVF_TEX1); }
static void pt_sprite_xyz(Ctx &c) { pt_setup(c, true, true); points(c, false, D3DFVF_TEX1); }
static void pt_sprite_notex_coords(Ctx &c) { pt_setup(c, true, true); points(c, false); }
static void pt_psize_rhw(Ctx &c)
{
    pt_setup(c, true, true);
    points(c, true, D3DFVF_PSIZE | D3DFVF_TEX1, [](V *v, int i) { v->psize = i & 1 ? 8.0f : 16.0f; });
}
static void pt_psize_xyz(Ctx &c)
{
    pt_setup(c, true, true);
    points(c, false, D3DFVF_PSIZE | D3DFVF_TEX1, [](V *v, int i) { v->psize = i & 1 ? 8.0f : 16.0f; });
}
static void pt_size_max(Ctx &c)
{
    pt_setup(c, true, true);
    rs(c, D3DRS_POINTSIZE_MAX, fbits(8.0f));
    points(c, false, D3DFVF_TEX1);
}
static void pt_size_min(Ctx &c)
{
    pt_setup(c, false, false);
    rs(c, D3DRS_POINTSIZE, fbits(2.0f));
    rs(c, D3DRS_POINTSIZE_MIN, fbits(16.0f));
    points(c, false);
}
/* Point scale: size = H * size * sqrt(1 / (A + B d + C d^2)), d the eye
 * distance. Perspective with the points at different depths. */
static void pt_scale(Ctx &c)
{
    set_xf(c, ident(), ident(), persp(1.0f, 1.0f, 0.5f, 20.0f));
    pt_setup(c, true, true);
    rs(c, D3DRS_POINTSIZE, fbits(0.5f));
    rs(c, D3DRS_POINTSCALEENABLE, TRUE);
    rs(c, D3DRS_POINTSCALE_A, fbits(0.0f));
    rs(c, D3DRS_POINTSCALE_B, fbits(0.0f));
    rs(c, D3DRS_POINTSCALE_C, fbits(1.0f));
    points(c, false, D3DFVF_TEX1, [](V *v, int i) {
        float d = i & 1 ? 2.0f : 1.5f;
        float t = tanf(0.5f);
        v->p[0] *= d * t; v->p[1] *= d * t; v->p[2] = d;
    });
}
static void pt_scale_ab(Ctx &c)
{
    set_xf(c, ident(), ident(), persp(1.0f, 1.0f, 0.5f, 20.0f));
    pt_setup(c, false, false);
    /* Eye-space size: 64 * 0.25 / sqrt(0.5 + 0.25 d) is 14 to 19 pixels,
     * below the reference rasterizer's MaxPointSize of 64. */
    rs(c, D3DRS_POINTSIZE, fbits(0.25f));
    rs(c, D3DRS_POINTSCALEENABLE, TRUE);
    rs(c, D3DRS_POINTSCALE_A, fbits(0.5f));
    rs(c, D3DRS_POINTSCALE_B, fbits(0.25f));
    rs(c, D3DRS_POINTSCALE_C, fbits(0.0f));
    points(c, false, 0, [](V *v, int i) {
        float d = 1.0f + i * 0.5f;
        float t = tanf(0.5f);
        v->p[0] *= d * t; v->p[1] *= d * t; v->p[2] = d;
    });
}

/* ---- untransformed geometry and transforms ---------------------------- */

static void xf_colour_quad(Ctx &c, const V *in)
{
    V q[4];
    memcpy(q, in, sizeof q);
    q[0].diff = 0xffff0000; q[1].diff = 0xff00ff00; q[2].diff = 0xff0000ff; q[3].diff = 0xffffffff;
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
}
/* World places a unit quad, view moves it, projection is orthographic. */
static void xf_world_view(Ctx &c)
{
    D3DMATRIX w = scale_tr(1.0f, 1.0f, 1, -0.25f, 0.5f, 0);
    D3DMATRIX v = scale_tr(1, 1, 1, -0.5f, -0.25f, 0.5f);
    D3DMATRIX p = scale_tr(1.0f, 1.0f, 0.5f, 0, 0, 0);
    set_xf(c, w, v, p);
    V q[4];
    quad_xyz(q, 0, 0, 1.25f, -1.0f, 0.0f);
    xf_colour_quad(c, q);
}
static void xf_persp(Ctx &c)
{
    set_xf(c, rot_y(0.6f), scale_tr(1, 1, 1, 0, 0, 2.5f), persp(1.0f, 1.0f, 0.5f, 10.0f));
    V q[4];
    quad_xyz(q, -1, 1, 1, -1, 0.0f);
    xf_colour_quad(c, q);
}
static void xf_persp_tex(Ctx &c)
{
    set_xf(c, rot_y(0.7f), scale_tr(1, 1, 1, 0, 0, 2.0f), persp(1.2f, 1.0f, 0.5f, 10.0f));
    V q[4];
    quad_xyz(q, -1, 1, 1, -1, 0.0f);
    tex(c, 0, c.grad);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_TEX1, q, 4);
}
static void xf_near_clip(Ctx &c)
{
    /* A floor that runs from behind the eye to far away: clipped at the
     * near plane. */
    set_xf(c, ident(), scale_tr(1, 1, 1, 0, 0.6f, 0), persp(1.2f, 1.0f, 0.5f, 10.0f));
    V q[4];
    q[0] = vtx(-2, 0, 8); q[1] = vtx(2, 0, 8); q[2] = vtx(-2, 0, -1); q[3] = vtx(2, 0, -1);
    corners_uv(q);
    tex(c, 0, c.grad);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    rs(c, D3DRS_CULLMODE, D3DCULL_NONE);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_TEX1, q, 4);
}
static void xf_cull(Ctx &c)
{
    /* Left quad wound clockwise, right one counter-clockwise; CULL_CCW
     * (the default) removes the right one. */
    V q[4];
    quad_xyz(q, -1, 1, 0, -1);
    xf_colour_quad(c, q);
    quad_xyz(q, 0, -1, 1, 1);
    xf_colour_quad(c, q);
}
static void xf_viewport(Ctx &c)
{
    D3DVIEWPORT9 vp = {8, 16, 40, 32, 0.0f, 1.0f};
    CK(c.dev->SetViewport(&vp));
    V q[4];
    quad_xyz(q, -1, 1, 1, -1);
    xf_colour_quad(c, q);
}
static void xf_indexed_vb(Ctx &c)
{
    set_xf(c, rot_y(0.3f), scale_tr(1, 1, 1, 0, 0, 2.0f), persp(1.0f, 1.0f, 0.5f, 10.0f));
    V q[4];
    quad_xyz(q, -1, 1, 1, -1, 0.0f);
    q[0].diff = 0xffff0000; q[1].diff = 0xff00ff00; q[2].diff = 0xff0000ff; q[3].diff = 0xffffffff;
    std::vector<float> buf;
    UINT stride;
    DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
    pack(fvf, q, 4, buf, &stride);
    IDirect3DVertexBuffer9 *vb = NULL;
    IDirect3DIndexBuffer9 *ib = NULL;
    CK(c.dev->CreateVertexBuffer(4 * stride, D3DUSAGE_WRITEONLY, fvf, D3DPOOL_MANAGED, &vb, NULL));
    CK(c.dev->CreateIndexBuffer(6 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL));
    if (!vb || !ib) return;
    void *p;
    if (SUCCEEDED(vb->Lock(0, 0, &p, 0))) { memcpy(p, buf.data(), 4 * stride); vb->Unlock(); }
    static const WORD idx[6] = {0, 1, 2, 2, 1, 3};
    if (SUCCEEDED(ib->Lock(0, 0, &p, 0))) { memcpy(p, idx, sizeof idx); ib->Unlock(); }
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    CK(c.dev->SetFVF(fvf));
    CK(c.dev->SetStreamSource(0, vb, 0, stride));
    CK(c.dev->SetIndices(ib));
    CK(c.dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2));
    c.dev->SetStreamSource(0, NULL, 0, 0);
    c.dev->SetIndices(NULL);
    vb->Release();
    ib->Release();
}
static void xf_clipplane(Ctx &c)
{
    float plane[4] = {1.0f, 0.5f, 0.0f, 0.25f}; /* keep x + 0.5y + 0.25 >= 0 */
    CK(c.dev->SetClipPlane(0, plane));
    rs(c, D3DRS_CLIPPLANEENABLE, D3DCLIPPLANE0);
    V q[4];
    quad_xyz(q, -1, 1, 1, -1);
    xf_colour_quad(c, q);
}
static void xf_vertexblend(Ctx &c)
{
    D3DMATRIX w0 = scale_tr(1, 1, 1, -0.5f, 0, 0), w1 = scale_tr(1, 1, 1, 0.5f, 0.25f, 0);
    CK(c.dev->SetTransform(D3DTS_WORLDMATRIX(0), &w0));
    CK(c.dev->SetTransform(D3DTS_WORLDMATRIX(1), &w1));
    rs(c, D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
    V q[4];
    quad_xyz(q, -0.5f, 0.75f, 0.5f, -0.75f);
    for (int i = 0; i < 4; i++) q[i].weight = (i & 1) ? 0.0f : 1.0f;
    q[0].diff = 0xffff0000; q[1].diff = 0xff00ff00; q[2].diff = 0xff0000ff; q[3].diff = 0xffffffff;
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZB1 | D3DFVF_DIFFUSE, q, 4);
}
static void xf_depth(Ctx &c)
{
    /* Two overlapping quads, the later one farther: the depth test keeps the
     * first where they overlap. */
    set_xf(c, ident(), ident(), persp(1.0f, 1.0f, 0.5f, 10.0f));
    V q[4];
    quad_xyz(q, -1.5f, 1.0f, 0.5f, -1.0f, 2.0f);
    for (int i = 0; i < 4; i++) q[i].diff = 0xffff8000;
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
    quad_xyz(q, -0.5f, 1.5f, 2.0f, -0.5f, 3.0f);
    for (int i = 0; i < 4; i++) q[i].diff = 0xff0080ff;
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
}
static void xf_flat_shade(Ctx &c)
{
    rs(c, D3DRS_SHADEMODE, D3DSHADE_FLAT);
    V q[4];
    quad_xyz(q, -1, 1, 1, -1);
    xf_colour_quad(c, q);
}
static void xf_lines(Ctx &c)
{
    V l[4];
    l[0] = vtx(-1.0f, 0.125f + 1.0f / 64, 0.5f); l[1] = vtx(1.0f, 0.125f + 1.0f / 64, 0.5f);
    l[2] = vtx(-0.875f + 1.0f / 64, 1, 0.5f); l[3] = vtx(-0.875f + 1.0f / 64, -1, 0.5f);
    l[0].diff = l[1].diff = 0xffffff00;
    l[2].diff = l[3].diff = 0xff00ffff;
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    draw(c, D3DPT_LINELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE, l, 4);
}

/* ---- the case table --------------------------------------------------- */

struct Case {
    const char *name;
    int tol;
    void (*fn)(Ctx &);
    int path; /* -1 none, else RHW or XYZ for the blend cases */
};

#define B2(n, t) {#n "-rhw", t, n, RHW}, {#n "-xyz", t, n, XYZ}
static const Case cases[] = {
    B2(b_sel_tex, 2), B2(b_sel_diff, 2), B2(b_sel_tf, 2), B2(b_sel_spec, 2),
    B2(b_mod, 3), B2(b_mod2x, 3), B2(b_mod4x, 4), B2(b_add, 2), B2(b_addsigned, 3),
    B2(b_addsigned2x, 4), B2(b_subtract, 2), B2(b_addsmooth, 3), B2(b_blendtexalpha, 3),
    B2(b_blenddiffalpha, 3), B2(b_blendfactoralpha, 3), B2(b_blendcurrentalpha, 3),
    B2(b_blendtexalphapm, 3), B2(b_modalpha_addcolor, 3), B2(b_modcolor_addalpha, 3),
    B2(b_dot3, 4), B2(b_dot3_alpha, 4), B2(b_lerp, 3), B2(b_multiplyadd, 3),
    B2(b_complement, 3), B2(b_alpharep, 3), B2(b_comp_alpharep, 3),
    B2(b_2stage_add, 3), B2(b_2stage_lightmap, 3), B2(b_3stage, 4), B2(b_4stage, 5),
    B2(b_disable_stage1, 3), B2(b_temp, 3), B2(b_specularenable, 3), B2(b_constant, 3),
    B2(b_defaults, 2),
    {"tg_identity", 2, tg_identity, -1},
    {"tg_rhw_identity", 2, tg_rhw_identity, -1},
    {"tg_index1", 2, tg_index1, -1},
    {"tg_count2", 3, tg_count2, -1},
    {"tg_count2_rhw", 3, tg_count2_rhw, -1},
    {"tg_count3_projected", 4, tg_count3_projected, -1},
    {"tg_count4_projected", 4, tg_count4_projected, -1},
    {"tg_campos", 4, tg_campos, -1},
    {"tg_campos_world", 4, tg_campos_world, -1},
    {"tg_camnormal", 4, tg_camnormal, -1},
    {"tg_camnormal_rot", 4, tg_camnormal_rot, -1},
    {"tg_camreflect", 4, tg_camreflect, -1},
    {"tg_camreflect_nolocal", 4, tg_camreflect_nolocal, -1},
    {"tg_sphere", 4, tg_sphere, -1},
    {"tg_count3_normal_projected", 4, tg_count3_cube, -1},
    {"fv_linear", 4, fv_linear, -1},
    {"fv_exp", 4, fv_exp, -1},
    {"fv_exp2", 4, fv_exp2, -1},
    {"ft_linear", 4, ft_linear, -1},
    {"ft_exp", 4, ft_exp, -1},
    {"ft_exp2", 4, ft_exp2, -1},
    {"fv_linear_tex", 4, fv_linear_tex, -1},
    {"fv_range", 4, fv_range, -1},
    {"ft_linear_persp", 4, ft_linear_persp, -1},
    {"ft_exp_persp", 4, ft_exp_persp, -1},
    {"fv_linear_persp", 4, fv_linear_persp, -1},
    {"fv_exp2_persp", 4, fv_exp2_persp, -1},
    {"fog_rhw_spec", 4, fog_rhw_spec, -1},
    {"fog_rhw_table", 4, fog_rhw_table, -1},
    {"fog_rhw_nospec", 4, fog_rhw_nospec, -1},
    {"lt_dir", 4, lt_dir, -1},
    {"lt_dir_spec", 5, lt_dir_spec, -1},
    {"lt_dir_spec_nolocal", 5, lt_dir_spec_nolocal, -1},
    {"lt_point", 4, lt_point, -1},
    {"lt_point_spec", 5, lt_point_spec, -1},
    {"lt_point_range", 4, lt_point_range, -1},
    {"lt_spot", 4, lt_spot, -1},
    {"lt_spot_falloff", 4, lt_spot_falloff, -1},
    {"lt_three", 5, lt_three, -1},
    {"lt_ambient_only", 3, lt_ambient_only, -1},
    {"lt_scaled_nonorm", 4, lt_scaled_nonorm, -1},
    {"lt_scaled_norm", 4, lt_scaled_norm, -1},
    {"lt_colorvertex", 4, lt_colorvertex, -1},
    {"lt_colorvertex_off", 4, lt_colorvertex_off, -1},
    {"lt_colorvertex_spec_src", 5, lt_colorvertex_spec_src, -1},
    {"lt_colorvertex_nodiffuse", 4, lt_colorvertex_nodiffuse, -1},
    {"lt_tex_modulate", 5, lt_tex_modulate, -1},
    {"lt_persp_point", 5, lt_persp_point, -1},
    {"lt_backface_cull_off", 4, lt_backface_cull_off, -1},
    {"at_greater", 2, at_greater, -1},
    {"at_greaterequal", 2, at_greaterequal, -1},
    {"at_less", 2, at_less, -1},
    {"at_lessequal", 2, at_lessequal, -1},
    {"at_notequal", 2, at_notequal, -1},
    {"at_never", 2, at_never, -1},
    {"at_xyz_diffuse", 3, at_xyz_diffuse, -1},
    {"ab_alpha", 3, ab_alpha, -1},
    {"ab_add", 3, ab_add, -1},
    {"ab_srcalpha_one", 3, ab_srcalpha_one, -1},
    {"ab_modulate", 3, ab_modulate, -1},
    {"ab_modulate2x", 3, ab_modulate2x, -1},
    {"ab_zero_srccolor", 3, ab_zero_srccolor, -1},
    {"ab_invdestcolor", 3, ab_invdestcolor, -1},
    {"ab_destalpha", 3, ab_destalpha, -1},
    {"ab_premul", 3, ab_premul, -1},
    {"ab_bothsrcalpha", 3, ab_bothsrcalpha, -1},
    {"ab_srcalphasat", 3, ab_srcalphasat, -1},
    {"ab_revsubtract", 3, ab_revsubtract, -1},
    {"ab_subtract", 3, ab_subtract, -1},
    {"ab_min", 3, ab_min, -1},
    {"ab_max", 3, ab_max, -1},
    {"ab_blendfactor", 3, ab_blendfactor, -1},
    {"ab_separate_alpha", 3, ab_separate_alpha, -1},
    {"ab_atest_and_blend", 3, ab_atest_and_blend, -1},
    {"ab_fog_blend", 4, ab_fog_blend, -1},
    {"pt_size_rhw", 2, pt_size_rhw, -1},
    {"pt_size_xyz", 2, pt_size_xyz, -1},
    {"pt_sprite_rhw", 3, pt_sprite_rhw, -1},
    {"pt_sprite_xyz", 3, pt_sprite_xyz, -1},
    {"pt_sprite_notex_coords", 3, pt_sprite_notex_coords, -1},
    {"pt_psize_rhw", 3, pt_psize_rhw, -1},
    {"pt_psize_xyz", 3, pt_psize_xyz, -1},
    {"pt_size_max", 3, pt_size_max, -1},
    {"pt_size_min", 2, pt_size_min, -1},
    {"pt_scale", 3, pt_scale, -1},
    {"pt_scale_ab", 2, pt_scale_ab, -1},
    {"xf_world_view", 3, xf_world_view, -1},
    {"xf_persp", 4, xf_persp, -1},
    {"xf_persp_tex", 3, xf_persp_tex, -1},
    {"xf_near_clip", 3, xf_near_clip, -1},
    {"xf_cull", 2, xf_cull, -1},
    {"xf_viewport", 3, xf_viewport, -1},
    {"xf_indexed_vb", 4, xf_indexed_vb, -1},
    {"xf_clipplane", 3, xf_clipplane, -1},
    {"xf_vertexblend", 3, xf_vertexblend, -1},
    {"xf_depth", 2, xf_depth, -1},
    {"xf_flat_shade", 2, xf_flat_shade, -1},
    {"xf_lines", 2, xf_lines, -1},
};

/* ---- driver ----------------------------------------------------------- */

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static const DWORD CLEARC = 0x80406080;

static bool run_case(IDirect3D9 *d3d, HWND hwnd, D3DDEVTYPE type, DWORD vp, const Case &k)
{
    Ctx c;
    memset(&c, 0, sizeof c);
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, type, hwnd, vp | D3DCREATE_FPU_PRESERVE, &pp, &c.dev);
    if (FAILED(hr)) {
        logf("CASE %s tol=%d hr=0x%08lx px=create-device-failed\n", k.name, k.tol, (unsigned long)hr);
        return false;
    }
    IDirect3DSurface9 *rt = NULL, *ds = NULL, *sys = NULL;
    CK(c.dev->CreateRenderTarget(W, H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, NULL));
    CK(c.dev->CreateDepthStencilSurface(W, H, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &ds, NULL));
    CK(c.dev->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL));
    if (rt) CK(c.dev->SetRenderTarget(0, rt));
    if (ds) CK(c.dev->SetDepthStencilSurface(ds));
    c.t0 = make_tex(c, 4, 4, f_solid, T0C);
    c.t1 = make_tex(c, 4, 4, f_solid, T1C);
    c.grad = make_tex(c, 256, 256, f_grad, 0);
    c.agrad = make_tex(c, 64, 4, f_agrad, 0);
    c.nmap = make_tex(c, 4, 4, f_solid, 0x40e0a060); /* normal (0.76, 0.25, -0.25) */
    c.white = make_tex(c, 4, 4, f_solid, 0xffffffff);
    D3DMATRIX id = ident();
    set_xf(c, id, id, id);
    CK(c.dev->SetRenderState(D3DRS_LIGHTING, FALSE));
    CK(c.dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
    CK(c.dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, CLEARC, 1.0f, 0));
    CK(c.dev->BeginScene());
    g_path = k.path == XYZ ? XYZ : RHW;
    k.fn(c);
    CK(c.dev->EndScene());
    char px[GRID * GRID * 9 + 1];
    px[0] = 0;
    if (rt && sys) {
        CK(c.dev->GetRenderTargetData(rt, sys));
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
            char *o = px;
            for (int j = 0; j < GRID; j++)
                for (int i = 0; i < GRID; i++) {
                    DWORD v = ((DWORD *)((BYTE *)lr.pBits + (4 + 8 * j) * lr.Pitch))[4 + 8 * i];
                    o += sprintf(o, "%s%08lx", (i || j) ? "," : "", (unsigned long)v);
                }
            sys->UnlockRect();
        }
    }
    /* Show the case in the window too. */
    IDirect3DSurface9 *bb = NULL;
    if (rt && SUCCEEDED(c.dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        c.dev->StretchRect(rt, NULL, bb, NULL, D3DTEXF_NONE);
        bb->Release();
        c.dev->Present(NULL, NULL, NULL, NULL);
    }
    logf("CASE %s tol=%d hr=0x%08lx px=%s\n", k.name, k.tol, (unsigned long)c.hr, px);
    IDirect3DTexture9 *ts[] = {c.t0, c.t1, c.grad, c.agrad, c.nmap, c.white};
    for (auto t : ts) if (t) t->Release();
    if (sys) sys->Release();
    if (ds) ds->Release();
    if (rt) rt->Release();
    c.dev->Release();
    return SUCCEEDED(c.hr);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    D3DDEVTYPE type = D3DDEVTYPE_HAL;
    DWORD vp = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    char logpath[MAX_PATH] = "";
    std::vector<std::string> only;
    for (int i = 1; i < argc; i++) {
        char a[512];
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, a, sizeof a, NULL, NULL);
        if (!strcmp(a, "--ref")) type = D3DDEVTYPE_REF;
        else if (!strcmp(a, "--swvp")) vp = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        else if (!strcmp(a, "--log") && i + 1 < argc)
            WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, logpath, sizeof logpath, NULL, NULL);
        else if (!strcmp(a, "--only") && i + 1 < argc) {
            WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, a, sizeof a, NULL, NULL);
            only.push_back(a);
        }
    }
    if (!logpath[0]) {
        GetModuleFileNameA(NULL, logpath, MAX_PATH);
        char *s = strrchr(logpath, '\\');
        snprintf(s ? s + 1 : logpath, MAX_PATH - (s ? (s + 1 - logpath) : 0), "d3d9_ffp.txt");
    }
    logf_ = fopen(logpath, "w");
    if (!logf_) return 255;
    logf("d3d9_ffp device=%s vp=%s arch=%s\n", type == D3DDEVTYPE_REF ? "ref" : "hal",
         vp == D3DCREATE_SOFTWARE_VERTEXPROCESSING ? "sw" : "hw", sizeof(void *) == 8 ? "x64" : "x86");

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = L"d3d9_ffp";
    RegisterClassW(&wc);
    RECT r = {0, 0, 256, 256};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"d3d9_ffp", L"d3d9_ffp", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { logf("Direct3DCreate9 failed\n"); return 255; }
    D3DCAPS9 caps;
    if (SUCCEEDED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, type, &caps)))
        logf("caps DevCaps=0x%08lx MaxTextureBlendStages=%lu MaxSimultaneousTextures=%lu "
             "TextureOpCaps=0x%08lx FVFCaps=0x%08lx VertexProcessingCaps=0x%08lx MaxActiveLights=%lu "
             "PrimitiveMiscCaps=0x%08lx RasterCaps=0x%08lx MaxPointSize=%g VS=%lx PS=%lx\n",
             caps.DevCaps, caps.MaxTextureBlendStages, caps.MaxSimultaneousTextures, caps.TextureOpCaps,
             caps.FVFCaps, caps.VertexProcessingCaps, caps.MaxActiveLights, caps.PrimitiveMiscCaps,
             caps.RasterCaps, caps.MaxPointSize, caps.VertexShaderVersion, caps.PixelShaderVersion);
    else
        logf("GetDeviceCaps failed\n");

    int ran = 0, failed = 0;
    for (const Case &k : cases) {
        bool want = only.empty();
        for (auto &o : only) if (strstr(k.name, o.c_str())) want = true;
        if (!want) continue;
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        ran++;
        if (!run_case(d3d, hwnd, type, vp, k)) failed++;
    }
    logf("RESULT ran=%d failed_hr=%d\n", ran, failed);
    d3d->Release();
    fclose(logf_);
    return failed;
}
