/* ffp2: D3D9 fixed-function coverage, second part. The harness is the one
 * of docs/evidence/ffp/src/d3d9_ffp.cpp: each case renders into a 64x64
 * A8R8G8B8 render target on its own device, reads it back with
 * GetRenderTargetData and logs an 8x8 grid of pixels (at 4 + 8i), so the
 * same run on the reference rasterizer gives the expected values. The cases
 * are the ones d3d9_ffp leaves out: vertex blending with more than one
 * weight, indexed vertex blending and tweening (vb_*), bump environment
 * mapping (bm_*), texture coordinates into cube and volume textures (cm_*,
 * vol_*), and devices with D3DCREATE_MIXED_VERTEXPROCESSING that switch
 * SetSoftwareVertexProcessing between draws (mx_*).
 *
 *   d3d9_ffp2.exe [--ref] [--swvp] [--mixed] [--log FILE] [--only SUBSTR]...
 *
 *   --ref    D3DDEVTYPE_REF (d3dref9.dll from the DirectX SDK in the system
 *            directory) instead of D3DDEVTYPE_HAL
 *   --swvp   software vertex processing (default: hardware)
 *   --mixed  create every device with D3DCREATE_MIXED_VERTEXPROCESSING and
 *            select hardware, or with --swvp software, vertex processing
 *            with SetSoftwareVertexProcessing
 *   --only   run the cases whose name contains SUBSTR (repeatable)
 *
 * The mx_* cases always create a mixed device and switch it themselves.
 *
 * Log lines:
 *   CASE <name> tol=<n> hr=<first failed HRESULT or 0> px=<64 x AARRGGBB>
 *   RESULT ran=<n> failed_hr=<n>
 * Compare two logs with docs/evidence/ffp/src/compare.py. Exit code:
 * number of cases with an HRESULT failure.
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
    IDirect3DTexture9 *grad, *t0;
    IDirect3DCubeTexture9 *cube;
    IDirect3DVolumeTexture9 *vol;
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

static IDirect3DTexture9 *make_tex(Ctx &c, UINT w, UINT h, DWORD (*f)(UINT, UINT, UINT, UINT, DWORD), DWORD arg)
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

/* Cube faces of 128x128: red and green are the face's u and v, blue names
 * the face. */
static IDirect3DCubeTexture9 *make_cube(Ctx &c)
{
    IDirect3DCubeTexture9 *t = NULL;
    CK(c.dev->CreateCubeTexture(128, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL));
    if (!t) return NULL;
    for (int f = 0; f < 6; f++) {
        D3DLOCKED_RECT lr;
        if (FAILED(t->LockRect((D3DCUBEMAP_FACES)f, 0, &lr, NULL, 0))) continue;
        for (UINT y = 0; y < 128; y++) {
            DWORD *row = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
            for (UINT x = 0; x < 128; x++) row[x] = 0xff000000 | (x * 2 << 16) | (y * 2 << 8) | (0x18 + f * 0x2a);
        }
        t->UnlockRect((D3DCUBEMAP_FACES)f, 0);
    }
    return t;
}
/* 32x32x8 volume: red = u, green = v, blue = w. */
static IDirect3DVolumeTexture9 *make_vol(Ctx &c)
{
    IDirect3DVolumeTexture9 *t = NULL;
    CK(c.dev->CreateVolumeTexture(32, 32, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL));
    if (!t) return NULL;
    D3DLOCKED_BOX lb;
    if (SUCCEEDED(t->LockBox(0, &lb, NULL, 0))) {
        for (UINT z = 0; z < 8; z++)
            for (UINT y = 0; y < 32; y++) {
                DWORD *row = (DWORD *)((BYTE *)lb.pBits + z * lb.SlicePitch + y * lb.RowPitch);
                for (UINT x = 0; x < 32; x++) row[x] = 0xff000000 | (x * 8 << 16) | (y * 8 << 8) | (z * 32 + 16);
            }
        t->UnlockBox(0);
    }
    return t;
}

/* ---- vertices --------------------------------------------------------- */

struct V {
    float p[4];
    float beta[4];
    DWORD idx; /* matrix indices, UBYTE4 or D3DCOLOR */
    float n[3];
    DWORD diff, spec;
    float tc[4][4];
};

static V vtx(float x, float y, float z, float w = 1.0f)
{
    V v;
    memset(&v, 0, sizeof v);
    v.p[0] = x; v.p[1] = y; v.p[2] = z; v.p[3] = w;
    v.n[2] = -1.0f;
    v.diff = 0xff80c040;
    v.spec = 0xff000000;
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
    DWORD pos = fvf & D3DFVF_POSITION_MASK;
    int nbeta = pos == D3DFVF_XYZB1 ? 1 : pos == D3DFVF_XYZB2 ? 2 : pos == D3DFVF_XYZB3 ? 3 :
                pos == D3DFVF_XYZB4 ? 4 : pos == D3DFVF_XYZB5 ? 5 : 0;
    bool last_idx = (fvf & (D3DFVF_LASTBETA_UBYTE4 | D3DFVF_LASTBETA_D3DCOLOR)) != 0;
    for (int k = 0; k < n; k++) {
        const V &v = in[k];
        size_t start = out.size();
        float f;
        out.push_back(v.p[0]); out.push_back(v.p[1]); out.push_back(v.p[2]);
        if (pos == D3DFVF_XYZRHW) out.push_back(v.p[3]);
        for (int b = 0; b < nbeta; b++) {
            if (last_idx && b == nbeta - 1) { memcpy(&f, &v.idx, 4); out.push_back(f); }
            else out.push_back(v.beta[b]);
        }
        if (fvf & D3DFVF_NORMAL) { out.push_back(v.n[0]); out.push_back(v.n[1]); out.push_back(v.n[2]); }
        if (fvf & D3DFVF_DIFFUSE) { memcpy(&f, &v.diff, 4); out.push_back(f); }
        if (fvf & D3DFVF_SPECULAR) { memcpy(&f, &v.spec, 4); out.push_back(f); }
        int ntc = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
        for (int i = 0; i < ntc; i++)
            for (int j = 0; j < fvf_tc_size(fvf, i); j++) out.push_back(v.tc[i][j]);
        *stride = (UINT)((out.size() - start) * 4);
    }
}

static UINT prim_count(D3DPRIMITIVETYPE prim, int n)
{
    return prim == D3DPT_TRIANGLESTRIP ? n - 2 : prim == D3DPT_TRIANGLELIST ? n / 3 : prim == D3DPT_POINTLIST ? n :
           prim == D3DPT_LINELIST ? n / 2 : n - 1;
}

static void draw(Ctx &c, D3DPRIMITIVETYPE prim, DWORD fvf, const V *v, int n)
{
    std::vector<float> buf;
    UINT stride;
    pack(fvf, v, n, buf, &stride);
    CK(c.dev->SetFVF(fvf));
    CK(c.dev->DrawPrimitiveUP(prim, prim_count(prim, n), buf.data(), stride));
}

/* The same through a vertex buffer, for mixed devices (the buffer needs
 * D3DUSAGE_SOFTWAREPROCESSING to be drawn with software processing). */
static void draw_vb(Ctx &c, D3DPRIMITIVETYPE prim, DWORD fvf, const V *v, int n, DWORD usage)
{
    std::vector<float> buf;
    UINT stride;
    pack(fvf, v, n, buf, &stride);
    IDirect3DVertexBuffer9 *vb = NULL;
    CK(c.dev->CreateVertexBuffer(n * stride, D3DUSAGE_WRITEONLY | usage, fvf, D3DPOOL_DEFAULT, &vb, NULL));
    if (!vb) return;
    void *p;
    if (SUCCEEDED(vb->Lock(0, 0, &p, 0))) { memcpy(p, buf.data(), n * stride); vb->Unlock(); }
    CK(c.dev->SetFVF(fvf));
    CK(c.dev->SetStreamSource(0, vb, 0, stride));
    CK(c.dev->DrawPrimitive(prim, 0, prim_count(prim, n)));
    c.dev->SetStreamSource(0, NULL, 0, 0);
    vb->Release();
}

static void corners_uv(V q[4])
{
    for (int i = 0; i < 4; i++) {
        float u = (float)(i & 1), v = (float)(i >> 1);
        for (int s = 0; s < 4; s++) { q[i].tc[s][0] = u; q[i].tc[s][1] = v; }
    }
}
static void quad_rhw(V q[4], float x0, float y0, float x1, float y1, float z = 0.5f)
{
    q[0] = vtx(x0, y0, z); q[1] = vtx(x1, y0, z); q[2] = vtx(x0, y1, z); q[3] = vtx(x1, y1, z);
    corners_uv(q);
}
static void quad_xyz(V q[4], float x0, float y0, float x1, float y1, float z = 0.5f)
{
    q[0] = vtx(x0, y0, z); q[1] = vtx(x1, y0, z); q[2] = vtx(x0, y1, z); q[3] = vtx(x1, y1, z);
    corners_uv(q);
}

/* A grid of N x N cells from -R to R in x and y as a triangle list. The
 * default colour is a gradient, so where a vertex lands shows in the
 * read-back; the normals lean out from the centre like a dome. */
typedef void (*Edit)(V *, float x, float y, int i, int j);
static std::vector<V> grid(int N, float R, Edit edit)
{
    std::vector<V> tri;
    auto at = [&](int i, int j) {
        float x = -R + 2.0f * R * i / N, y = R - 2.0f * R * j / N;
        V v = vtx(x, y, 0.5f);
        float nx = x / R * 0.6f, ny = y / R * 0.6f;
        v.n[0] = nx; v.n[1] = ny; v.n[2] = -sqrtf(fmaxf(0.02f, 1.0f - nx * nx - ny * ny));
        v.tc[0][0] = (float)i / N; v.tc[0][1] = (float)j / N;
        v.diff = 0xff000000 | ((DWORD)(i * 255 / N) << 16) | ((DWORD)(j * 255 / N) << 8) | 0x80;
        if (edit) edit(&v, x, y, i, j);
        return v;
    };
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            V a = at(i, j), b = at(i + 1, j), cc = at(i, j + 1), d = at(i + 1, j + 1);
            tri.push_back(a); tri.push_back(b); tri.push_back(cc);
            tri.push_back(cc); tri.push_back(b); tri.push_back(d);
        }
    return tri;
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
static D3DMATRIX mul(const D3DMATRIX &a, const D3DMATRIX &b)
{
    D3DMATRIX r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}
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
static D3DMATRIX rot_x(float a)
{
    D3DMATRIX m = ident();
    m._22 = cosf(a); m._23 = sinf(a);
    m._32 = -sinf(a); m._33 = cosf(a);
    return m;
}
static D3DMATRIX rot_z(float a)
{
    D3DMATRIX m = ident();
    m._11 = cosf(a); m._12 = sinf(a);
    m._21 = -sinf(a); m._22 = cosf(a);
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
static void tex(Ctx &c, int s, IDirect3DBaseTexture9 *t) { CK(c.dev->SetTexture(s, t)); }
static void point_filter(Ctx &c, int s)
{
    CK(c.dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT));
    CK(c.dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
    CK(c.dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
    CK(c.dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
    CK(c.dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
    CK(c.dev->SetSamplerState(s, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP));
}
static void select_diffuse(Ctx &c)
{
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
}
static void select_tex(Ctx &c, IDirect3DBaseTexture9 *t)
{
    tex(c, 0, t);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
}

/* Camera-space vectors in -1..1 mapped to 0..1 by the texture matrix. */
static void texgen2(Ctx &c, int s, DWORD tci)
{
    D3DMATRIX m = ident();
    m._11 = 0.5f; m._22 = -0.5f; m._41 = 0.5f; m._42 = 0.5f;
    CK(c.dev->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + s), &m));
    tss(c, s, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    tss(c, s, D3DTSS_TEXCOORDINDEX, tci);
}

static void light_setup(Ctx &c)
{
    D3DMATERIAL9 m;
    memset(&m, 0, sizeof m);
    m.Diffuse = {0.8f, 0.6f, 0.4f, 1.0f};
    m.Ambient = {0.3f, 0.5f, 0.2f, 1.0f};
    CK(c.dev->SetMaterial(&m));
    D3DLIGHT9 l;
    memset(&l, 0, sizeof l);
    l.Type = D3DLIGHT_DIRECTIONAL;
    l.Diffuse = {1.0f, 0.9f, 0.8f, 1.0f};
    l.Direction = {0.5f, -0.3f, 0.8f};
    CK(c.dev->SetLight(0, &l));
    CK(c.dev->LightEnable(0, TRUE));
    rs(c, D3DRS_LIGHTING, TRUE);
    rs(c, D3DRS_AMBIENT, 0x00202830);
    rs(c, D3DRS_COLORVERTEX, FALSE);
}

/* ---- vertex blending -------------------------------------------------- */

/* World matrices that each map the grid's square of half-size 1.5 over the
 * whole target, so the blended mesh always covers it: scales of at least
 * 0.9, small rotations and translations, and different depths. */
static D3DMATRIX blend_matrix(int k)
{
    switch (k % 5) {
    case 0: return ident();
    case 1: return scale_tr(0.9f, 0.95f, 1.0f, 0.12f, -0.08f, 0.2f);
    case 2: return mul(rot_z(0.18f), scale_tr(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, -0.1f));
    case 3: return scale_tr(1.15f, 0.92f, 0.8f, -0.1f, 0.1f, 0.15f);
    default: return mul(rot_z(-0.15f), scale_tr(0.95f, 1.1f, 1.0f, 0.05f, 0.1f, 0.0f));
    }
}
static void set_blend_matrices(Ctx &c, const int *index, int n)
{
    for (int k = 0; k < n; k++) {
        D3DMATRIX m = blend_matrix(k);
        CK(c.dev->SetTransform(D3DTS_WORLDMATRIX(index ? index[k] : k), &m));
    }
}
/* Smooth weights over the grid; the last one is implied (1 - the rest). */
static void blend_weights(V *v, float x, float y, int, int)
{
    float u = (x + 1.5f) / 3.0f, t = (y + 1.5f) / 3.0f;
    v->beta[0] = 0.15f + 0.6f * u;
    v->beta[1] = 0.5f * t * (1.0f - u);
    v->beta[2] = 0.25f * (1.0f - t);
    v->beta[3] = 0.0f;
}
static void vb_grid(Ctx &c, DWORD fvf, DWORD vbf, Edit edit = blend_weights)
{
    set_blend_matrices(c, NULL, 4);
    rs(c, D3DRS_VERTEXBLEND, vbf);
    std::vector<V> g = grid(12, 1.5f, edit);
    draw(c, D3DPT_TRIANGLELIST, fvf, g.data(), (int)g.size());
}
static void vb_0weights(Ctx &c) { select_diffuse(c); vb_grid(c, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DVBF_0WEIGHTS); }
static void vb_1weight(Ctx &c) { select_diffuse(c); vb_grid(c, D3DFVF_XYZB1 | D3DFVF_DIFFUSE, D3DVBF_1WEIGHTS); }
static void vb_2weights(Ctx &c) { select_diffuse(c); vb_grid(c, D3DFVF_XYZB2 | D3DFVF_DIFFUSE, D3DVBF_2WEIGHTS); }
static void vb_3weights(Ctx &c) { select_diffuse(c); vb_grid(c, D3DFVF_XYZB3 | D3DFVF_DIFFUSE, D3DVBF_3WEIGHTS); }
/* More weights in the vertex than VERTEXBLEND uses. */
static void vb_1of3weights(Ctx &c) { select_diffuse(c); vb_grid(c, D3DFVF_XYZB3 | D3DFVF_DIFFUSE, D3DVBF_1WEIGHTS); }
static void vb_2weights_lit(Ctx &c)
{
    light_setup(c);
    select_diffuse(c);
    vb_grid(c, D3DFVF_XYZB2 | D3DFVF_NORMAL, D3DVBF_2WEIGHTS);
}
static void vb_3weights_lit_persp(Ctx &c)
{
    set_xf(c, ident(), scale_tr(1, 1, 1, 0, 0, 2.0f), persp(1.4f, 1.0f, 0.5f, 10.0f));
    light_setup(c);
    select_diffuse(c);
    vb_grid(c, D3DFVF_XYZB3 | D3DFVF_NORMAL, D3DVBF_3WEIGHTS);
}
/* The blended camera-space position read back through the gradient. */
static void vb_2weights_texgen(Ctx &c)
{
    select_tex(c, c.grad);
    texgen2(c, 0, D3DTSS_TCI_CAMERASPACEPOSITION);
    vb_grid(c, D3DFVF_XYZB2 | D3DFVF_NORMAL | D3DFVF_TEX1, D3DVBF_2WEIGHTS);
}
static void vb_3weights_normal(Ctx &c)
{
    select_tex(c, c.grad);
    texgen2(c, 0, D3DTSS_TCI_CAMERASPACENORMAL);
    vb_grid(c, D3DFVF_XYZB3 | D3DFVF_NORMAL | D3DFVF_TEX1, D3DVBF_3WEIGHTS);
}

/* Indexed: matrix indices per vertex, from a set of world matrices with
 * gaps between their indices, up to 8: the D3D9 runtime's fixed-function
 * vertex shader holds world matrices 0 to 8 in its constants (from c175, 8
 * registers each), so hardware vertex processing supports no more. The
 * UBYTE4 index of weight k sits in byte k. */
static const int IDX[4] = {0, 3, 5, 8};
static const int IDX_SW[4] = {0, 40, 129, 255};
static void idx_pick(V *v, int i, int j, const int *set)
{
    int a = (i + j) & 3, b = (i / 3 + 1) & 3, cc = (j / 2 + 2) & 3, d = (i * j + 3) & 3;
    v->idx = (DWORD)set[a] | (DWORD)set[b] << 8 | (DWORD)set[cc] << 16 | (DWORD)set[d] << 24;
}
static void set_indexed_matrices(Ctx &c, const int *set)
{
    /* blend_matrix(k) at index set[k]; the vertex picks the matrix by
     * index, so a mix-up of the indices moves the mesh. */
    set_blend_matrices(c, set, 4);
}
static void vb_indexed(Ctx &c, DWORD fvf, DWORD vbf, const int *set, Edit edit)
{
    set_indexed_matrices(c, set);
    rs(c, D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
    rs(c, D3DRS_VERTEXBLEND, vbf);
    std::vector<V> g = grid(12, 1.5f, edit);
    draw(c, D3DPT_TRIANGLELIST, fvf, g.data(), (int)g.size());
}
/* One matrix per vertex, picked by index, weight 1. */
static void vb_indexed_0w(Ctx &c)
{
    select_diffuse(c);
    vb_indexed(c, D3DFVF_XYZB1 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE, D3DVBF_0WEIGHTS, IDX,
               [](V *v, float, float, int i, int j) { idx_pick(v, i / 4, j / 4, IDX); });
}
static void vb_indexed_1w(Ctx &c)
{
    select_diffuse(c);
    vb_indexed(c, D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE, D3DVBF_1WEIGHTS, IDX,
               [](V *v, float x, float y, int i, int j) { blend_weights(v, x, y, i, j); idx_pick(v, i, j, IDX); });
}
static void vb_indexed_2w(Ctx &c)
{
    select_diffuse(c);
    vb_indexed(c, D3DFVF_XYZB3 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE, D3DVBF_2WEIGHTS, IDX,
               [](V *v, float x, float y, int i, int j) { blend_weights(v, x, y, i, j); idx_pick(v, i, j, IDX); });
}
static void vb_indexed_3w(Ctx &c)
{
    select_diffuse(c);
    vb_indexed(c, D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE, D3DVBF_3WEIGHTS, IDX,
               [](V *v, float x, float y, int i, int j) { blend_weights(v, x, y, i, j); idx_pick(v, i, j, IDX); });
}
/* D3DCOLOR indices: the runtime swizzles them like a colour. */
static void vb_indexed_color(Ctx &c)
{
    select_diffuse(c);
    vb_indexed(c, D3DFVF_XYZB3 | D3DFVF_LASTBETA_D3DCOLOR | D3DFVF_DIFFUSE, D3DVBF_2WEIGHTS, IDX,
               [](V *v, float x, float y, int i, int j) { blend_weights(v, x, y, i, j); idx_pick(v, i, j, IDX); });
}
static void vb_indexed_lit(Ctx &c)
{
    light_setup(c);
    select_diffuse(c);
    vb_indexed(c, D3DFVF_XYZB3 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL, D3DVBF_2WEIGHTS, IDX,
               [](V *v, float x, float y, int i, int j) { blend_weights(v, x, y, i, j); idx_pick(v, i, j, IDX); });
}
static void vb_indexed_texgen(Ctx &c)
{
    select_tex(c, c.grad);
    texgen2(c, 0, D3DTSS_TCI_CAMERASPACEPOSITION);
    vb_indexed(c, D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL | D3DFVF_TEX1, D3DVBF_1WEIGHTS, IDX,
               [](V *v, float x, float y, int i, int j) { blend_weights(v, x, y, i, j); idx_pick(v, i, j, IDX); });
}
/* Indices up to 255: software vertex processing supports 256 matrices,
 * hardware what MaxVertexBlendMatrixIndex says. Run by mx_indexed_high on a
 * mixed device in software vertex processing. */
static void vb_indexed_high(Ctx &c)
{
    select_diffuse(c);
    vb_indexed(c, D3DFVF_XYZB3 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE, D3DVBF_2WEIGHTS, IDX_SW,
               [](V *v, float x, float y, int i, int j) { blend_weights(v, x, y, i, j); idx_pick(v, i, j, IDX_SW); });
}
/* Tweening: position = POSITION0 + TWEENFACTOR * (POSITION1 - POSITION0). */
static void vb_tween(Ctx &c, bool lit)
{
    static const D3DVERTEXELEMENT9 el[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
        {0, 24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 36, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 1},
        {0, 48, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END()};
    IDirect3DVertexDeclaration9 *decl = NULL;
    CK(c.dev->CreateVertexDeclaration(el, &decl));
    if (!decl) return;
    std::vector<V> g = grid(12, 1.5f, NULL);
    std::vector<float> buf;
    for (const V &v : g) {
        float f;
        buf.push_back(v.p[0]); buf.push_back(v.p[1]); buf.push_back(v.p[2]);
        /* POSITION1: the grid rotated and shrunk a little. */
        buf.push_back(v.p[0] * 0.92f + v.p[1] * 0.2f); buf.push_back(v.p[1] * 0.9f - v.p[0] * 0.2f + 0.05f);
        buf.push_back(v.p[2] + 0.3f);
        buf.push_back(v.n[0]); buf.push_back(v.n[1]); buf.push_back(v.n[2]);
        buf.push_back(-v.n[1]); buf.push_back(v.n[0]); buf.push_back(v.n[2]);
        memcpy(&f, &v.diff, 4); buf.push_back(f);
    }
    if (lit) light_setup(c);
    select_diffuse(c);
    rs(c, D3DRS_VERTEXBLEND, D3DVBF_TWEENING);
    rs(c, D3DRS_TWEENFACTOR, fbits(0.6f));
    CK(c.dev->SetVertexDeclaration(decl));
    CK(c.dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)g.size() / 3, buf.data(), 52));
    decl->Release();
}
static void vb_tween_color(Ctx &c) { vb_tween(c, false); }
static void vb_tween_lit(Ctx &c) { vb_tween(c, true); }

/* ---- bump environment mapping ----------------------------------------- */

static bool fmt_ok(Ctx &c, D3DFORMAT f)
{
    IDirect3D9 *d3d = NULL;
    D3DDEVICE_CREATION_PARAMETERS cp;
    if (FAILED(c.dev->GetDirect3D(&d3d))) return false;
    c.dev->GetCreationParameters(&cp);
    HRESULT hr = d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, f);
    d3d->Release();
    if (FAILED(hr)) logf("  format %d not supported hr=0x%08lx\n", (int)f, (unsigned long)hr);
    return SUCCEEDED(hr);
}

/* 16x16 bump map. du runs from -0.5 to 0.5 across, dv from 0.375 to
 * -0.375 down (as signed 8-bit values over 127); luminance, where the
 * format has it, runs 0..1 across. */
static IDirect3DTexture9 *make_bump(Ctx &c, D3DFORMAT f, bool varying)
{
    if (!fmt_ok(c, f)) { CK(D3DERR_NOTAVAILABLE); return NULL; }
    IDirect3DTexture9 *t = NULL;
    CK(c.dev->CreateTexture(16, 16, 1, 0, f, D3DPOOL_MANAGED, &t, NULL));
    if (!t) return NULL;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(t->LockRect(0, &lr, NULL, 0))) {
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                float du = varying ? (x - 7.5f) / 15.0f : 0.25f, dv = varying ? (7.5f - y) / 20.0f : -0.125f;
                float l = varying ? x / 15.0f : 0.75f;
                BYTE *p = (BYTE *)lr.pBits + y * lr.Pitch;
                int iu = (int)lroundf(du * 127), iv = (int)lroundf(dv * 127);
                if (f == D3DFMT_V8U8) {
                    p[x * 2] = (BYTE)(signed char)iu;
                    p[x * 2 + 1] = (BYTE)(signed char)iv;
                } else if (f == D3DFMT_X8L8V8U8) {
                    p[x * 4] = (BYTE)(signed char)iu;
                    p[x * 4 + 1] = (BYTE)(signed char)iv;
                    p[x * 4 + 2] = (BYTE)lroundf(l * 255);
                    p[x * 4 + 3] = 0xff;
                } else { /* L6V5U5 */
                    int u5 = (int)lroundf(du * 15) & 31, v5 = (int)lroundf(dv * 15) & 31, l6 = (int)lroundf(l * 63);
                    WORD w = (WORD)(u5 | v5 << 5 | l6 << 10);
                    memcpy(p + x * 2, &w, 2);
                }
            }
        t->UnlockRect(0);
    }
    return t;
}
static void bump_mat(Ctx &c, int s, float m00, float m01, float m10, float m11)
{
    tss(c, s, D3DTSS_BUMPENVMAT00, fbits(m00));
    tss(c, s, D3DTSS_BUMPENVMAT01, fbits(m01));
    tss(c, s, D3DTSS_BUMPENVMAT10, fbits(m10));
    tss(c, s, D3DTSS_BUMPENVMAT11, fbits(m11));
}
/* Stage 0 perturbs stage 1's coordinates; stage 1 reads them back from
 * the gradient. The gradient's coordinates run 0.2..0.8 so the offsets stay
 * inside it. */
static void bump_quad(Ctx &c, bool rhw, DWORD op, D3DFORMAT f, bool varying, int stage = 0)
{
    IDirect3DTexture9 *b = make_bump(c, f, varying);
    if (!b) return;
    if (stage == 1) {
        tex(c, 0, c.t0);
        point_filter(c, 0);
        cop(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        aop(c, 0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
    }
    tex(c, stage, b);
    point_filter(c, stage);
    cop(c, stage, op, D3DTA_TEXTURE, D3DTA_CURRENT);
    aop(c, stage, D3DTOP_SELECTARG1, D3DTA_CURRENT);
    tss(c, stage, D3DTSS_TEXCOORDINDEX, stage);
    tex(c, stage + 1, c.grad);
    point_filter(c, stage + 1);
    cop(c, stage + 1, stage == 1 ? D3DTOP_ADD : D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
    aop(c, stage + 1, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    tss(c, stage + 1, D3DTSS_TEXCOORDINDEX, stage + 1);
    V q[4];
    if (rhw) quad_rhw(q, 0, 0, W, H); else quad_xyz(q, -1, 1, 1, -1);
    for (int i = 0; i < 4; i++) {
        q[i].tc[stage + 1][0] = 0.2f + 0.6f * (i & 1);
        q[i].tc[stage + 1][1] = 0.2f + 0.6f * (i >> 1);
    }
    DWORD fvf = (rhw ? D3DFVF_XYZRHW : D3DFVF_XYZ) | D3DFVF_DIFFUSE | (stage == 1 ? D3DFVF_TEX3 : D3DFVF_TEX2);
    draw(c, D3DPT_TRIANGLESTRIP, fvf, q, 4);
    b->Release();
}
static void bm_const_xyz(Ctx &c) { bump_mat(c, 0, 0.4f, 0.0f, 0.0f, 0.4f); bump_quad(c, false, D3DTOP_BUMPENVMAP, D3DFMT_V8U8, false); }
static void bm_const_rhw(Ctx &c) { bump_mat(c, 0, 0.4f, 0.0f, 0.0f, 0.4f); bump_quad(c, true, D3DTOP_BUMPENVMAP, D3DFMT_V8U8, false); }
static void bm_varying_xyz(Ctx &c) { bump_mat(c, 0, 0.3f, 0.0f, 0.0f, 0.3f); bump_quad(c, false, D3DTOP_BUMPENVMAP, D3DFMT_V8U8, true); }
static void bm_varying_rhw(Ctx &c) { bump_mat(c, 0, 0.3f, 0.0f, 0.0f, 0.3f); bump_quad(c, true, D3DTOP_BUMPENVMAP, D3DFMT_V8U8, true); }
/* Off-diagonal terms: u' = u + M00 du + M10 dv, v' = v + M01 du + M11 dv. */
static void bm_matrix(Ctx &c) { bump_mat(c, 0, 0.2f, -0.25f, 0.3f, 0.1f); bump_quad(c, false, D3DTOP_BUMPENVMAP, D3DFMT_V8U8, true); }
static void bm_x8l8v8u8(Ctx &c) { bump_mat(c, 0, 0.3f, 0.0f, 0.0f, 0.3f); bump_quad(c, false, D3DTOP_BUMPENVMAP, D3DFMT_X8L8V8U8, true); }
static void bm_lum_x8l8v8u8(Ctx &c)
{
    bump_mat(c, 0, 0.3f, 0.0f, 0.0f, 0.3f);
    tss(c, 0, D3DTSS_BUMPENVLSCALE, fbits(0.8f));
    tss(c, 0, D3DTSS_BUMPENVLOFFSET, fbits(0.1f));
    bump_quad(c, false, D3DTOP_BUMPENVMAPLUMINANCE, D3DFMT_X8L8V8U8, true);
}
static void bm_lum_l6v5u5(Ctx &c)
{
    bump_mat(c, 0, 0.3f, 0.0f, 0.0f, 0.3f);
    tss(c, 0, D3DTSS_BUMPENVLSCALE, fbits(1.2f));
    tss(c, 0, D3DTSS_BUMPENVLOFFSET, fbits(-0.1f));
    bump_quad(c, false, D3DTOP_BUMPENVMAPLUMINANCE, D3DFMT_L6V5U5, true);
}
static void bm_lum_rhw(Ctx &c)
{
    bump_mat(c, 0, 0.3f, 0.0f, 0.0f, 0.3f);
    tss(c, 0, D3DTSS_BUMPENVLSCALE, fbits(0.8f));
    tss(c, 0, D3DTSS_BUMPENVLOFFSET, fbits(0.1f));
    bump_quad(c, true, D3DTOP_BUMPENVMAPLUMINANCE, D3DFMT_X8L8V8U8, true);
}
/* The bump stage second: a base texture in stage 0, the bump map in
 * stage 1 and the environment added in stage 2. */
static void bm_stage1(Ctx &c) { bump_mat(c, 1, 0.3f, 0.0f, 0.0f, 0.3f); bump_quad(c, false, D3DTOP_BUMPENVMAP, D3DFMT_V8U8, true, 1); }
static void bm_stage1_lum(Ctx &c)
{
    bump_mat(c, 1, 0.3f, 0.1f, -0.1f, 0.3f);
    tss(c, 1, D3DTSS_BUMPENVLSCALE, fbits(0.9f));
    tss(c, 1, D3DTSS_BUMPENVLOFFSET, fbits(0.0f));
    bump_quad(c, false, D3DTOP_BUMPENVMAPLUMINANCE, D3DFMT_X8L8V8U8, true, 1);
}
/* The environment coordinates generated (camera-space normal) rather than
 * from the vertex. */
static void bm_texgen(Ctx &c)
{
    IDirect3DTexture9 *b = make_bump(c, D3DFMT_V8U8, true);
    if (!b) return;
    bump_mat(c, 0, 0.2f, 0.0f, 0.0f, 0.2f);
    tex(c, 0, b);
    point_filter(c, 0);
    cop(c, 0, D3DTOP_BUMPENVMAP, D3DTA_TEXTURE, D3DTA_CURRENT);
    aop(c, 0, D3DTOP_SELECTARG1, D3DTA_CURRENT);
    tex(c, 1, c.grad);
    point_filter(c, 1);
    cop(c, 1, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    aop(c, 1, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    D3DMATRIX m = ident();
    m._11 = 0.3f; m._22 = -0.3f; m._41 = 0.5f; m._42 = 0.5f;
    CK(c.dev->SetTransform(D3DTS_TEXTURE1, &m));
    tss(c, 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    tss(c, 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACENORMAL | 1);
    std::vector<V> g = grid(8, 1.0f, NULL);
    draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1, g.data(), (int)g.size());
    b->Release();
}

/* ---- cube and volume texture coordinates ------------------------------ */

static void cube_setup(Ctx &c) { select_tex(c, c.cube); }
/* A dome that is not symmetric in x and y, so no sample sits where two
 * components of a direction are equal (an edge between cube faces). */
static void cube_normal(V *v, float x, float y)
{
    float nx = x * 0.62f + 0.05f, ny = y * 0.47f - 0.06f;
    v->n[0] = nx; v->n[1] = ny; v->n[2] = -sqrtf(fmaxf(0.02f, 1.0f - nx * nx - ny * ny));
}
/* Explicit 3D coordinates: the dome's normals as directions. */
static void cm_explicit(Ctx &c)
{
    cube_setup(c);
    std::vector<V> g = grid(8, 1.0f, [](V *v, float x, float y, int, int) {
        cube_normal(v, x, y);
        v->tc[0][0] = v->n[0] * 1.3f; v->tc[0][1] = v->n[1] * 1.3f; v->tc[0][2] = -v->n[2];
    });
    draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0), g.data(),
         (int)g.size());
}
static void cm_explicit_rhw(Ctx &c)
{
    cube_setup(c);
    V q[4];
    quad_rhw(q, 0, 0, W, H);
    for (int i = 0; i < 4; i++) {
        q[i].tc[0][0] = (i & 1) ? 0.7f : -0.6f;
        q[i].tc[0][1] = (i >> 1) ? -0.5f : 0.8f;
        q[i].tc[0][2] = 1.0f;
    }
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0), q, 4);
}
/* Explicit coordinates through a 3x3 texture transform (COUNT3). */
static void cm_explicit_count3(Ctx &c)
{
    cube_setup(c);
    D3DMATRIX m = mul(rot_y(0.9f), rot_x(0.4f));
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
    std::vector<V> g = grid(8, 1.0f, [](V *v, float x, float y, int, int) {
        cube_normal(v, x, y);
        v->tc[0][0] = v->n[0]; v->tc[0][1] = v->n[1]; v->tc[0][2] = -v->n[2];
    });
    draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0), g.data(), (int)g.size());
}
static void cm_texgen(Ctx &c, DWORD tci, bool local = true, const D3DMATRIX *tm = NULL, bool persp_view = false)
{
    cube_setup(c);
    if (persp_view) set_xf(c, rot_y(0.5f), scale_tr(1, 1, 1, 0, 0, 2.5f), persp(1.2f, 1.0f, 0.5f, 10.0f));
    rs(c, D3DRS_LOCALVIEWER, local);
    tss(c, 0, D3DTSS_TEXCOORDINDEX, tci);
    if (tm) {
        CK(c.dev->SetTransform(D3DTS_TEXTURE0, tm));
        tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
    }
    std::vector<V> g = grid(8, 1.0f, [](V *v, float x, float y, int, int) { cube_normal(v, x, y); });
    draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_NORMAL, g.data(), (int)g.size());
}
static void cm_reflect(Ctx &c) { cm_texgen(c, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR); }
static void cm_reflect_nolocal(Ctx &c) { cm_texgen(c, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR, false); }
static void cm_reflect_count3(Ctx &c)
{
    D3DMATRIX m = mul(rot_y(0.7f), rot_x(-0.3f));
    cm_texgen(c, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR, true, &m);
}
static void cm_reflect_persp(Ctx &c) { cm_texgen(c, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR, true, NULL, true); }
static void cm_normal(Ctx &c) { cm_texgen(c, D3DTSS_TCI_CAMERASPACENORMAL); }
static void cm_normal_count3(Ctx &c)
{
    D3DMATRIX m = rot_y(2.4f);
    cm_texgen(c, D3DTSS_TCI_CAMERASPACENORMAL, true, &m);
}
static void cm_position(Ctx &c)
{
    /* Camera-space position plus an offset in the texture matrix's fourth
     * row, so the direction swings across faces. */
    D3DMATRIX m = ident();
    m._41 = 0.2f; m._42 = -0.1f; m._43 = -0.2f;
    cm_texgen(c, D3DTSS_TCI_CAMERASPACEPOSITION, true, &m);
}
/* Volume texture: explicit coordinates, then camera-space position. w is
 * affine in x and y, so it interpolates exactly, and at every sample it is
 * at least a tenth of a slice from a slice boundary. */
static void vol_explicit(Ctx &c)
{
    select_tex(c, c.vol);
    std::vector<V> g = grid(8, 1.0f, [](V *v, float x, float y, int, int) {
        v->tc[0][0] = (x + 1) * 0.5f; v->tc[0][1] = (1 - y) * 0.5f; v->tc[0][2] = 0.5f + 0.3f * x - 0.2f * y;
    });
    draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0), g.data(), (int)g.size());
}
static void vol_campos(Ctx &c)
{
    select_tex(c, c.vol);
    D3DMATRIX m = ident();
    m._11 = 0.5f; m._22 = -0.5f; m._33 = 1.5f; m._41 = 0.5f; m._42 = 0.5f; m._43 = -0.3f;
    CK(c.dev->SetTransform(D3DTS_TEXTURE0, &m));
    tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
    tss(c, 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
    std::vector<V> g = grid(8, 1.0f, [](V *v, float x, float y, int, int) { v->p[2] = 0.3f + 0.25f * (x - y); });
    draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_NORMAL, g.data(), (int)g.size());
}

/* ---- mixed vertex processing ------------------------------------------ */

static void swvp(Ctx &c, BOOL on) { CK(c.dev->SetSoftwareVertexProcessing(on)); }
/* Four quadrants, each lit and drawn with the processing given by the
 * bits of `sw` (top left first). */
static void mx_quadrants(Ctx &c, unsigned sw, bool vb)
{
    light_setup(c);
    select_diffuse(c);
    for (int k = 0; k < 4; k++) {
        float x0 = (k & 1) ? 0.0f : -1.0f, y0 = (k >> 1) ? 0.0f : 1.0f;
        std::vector<V> g = grid(4, 0.5f, NULL);
        for (V &v : g) { v.p[0] += x0 + 0.5f; v.p[1] += y0 - 0.5f; }
        swvp(c, (sw >> k) & 1);
        if (vb) draw_vb(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_NORMAL, g.data(), (int)g.size(),
                        D3DUSAGE_SOFTWAREPROCESSING);
        else draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_NORMAL, g.data(), (int)g.size());
    }
}
static void mx_hw_sw(Ctx &c) { mx_quadrants(c, 0x6, false); }
static void mx_sw_hw(Ctx &c) { mx_quadrants(c, 0x9, false); }
static void mx_all_sw(Ctx &c) { mx_quadrants(c, 0xf, false); }
static void mx_vb_hw_sw(Ctx &c) { mx_quadrants(c, 0x5, true); }
/* Each quadrant a different fixed-function feature, alternating. */
static void mx_features(Ctx &c, unsigned sw)
{
    /* Top left: vertex fog. */
    swvp(c, sw & 1);
    {
        rs(c, D3DRS_FOGENABLE, TRUE);
        rs(c, D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
        rs(c, D3DRS_FOGCOLOR, 0xff20ff80);
        rs(c, D3DRS_FOGSTART, fbits(0.2f));
        rs(c, D3DRS_FOGEND, fbits(0.8f));
        V q[4];
        quad_xyz(q, -1, 1, 0, 0);
        q[0].p[2] = q[2].p[2] = 0.0f;
        q[1].p[2] = q[3].p[2] = 1.0f;
        select_diffuse(c);
        draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
        rs(c, D3DRS_FOGENABLE, FALSE);
    }
    /* Top right: texgen through the gradient. */
    swvp(c, (sw >> 1) & 1);
    {
        select_tex(c, c.grad);
        texgen2(c, 0, D3DTSS_TCI_CAMERASPACENORMAL);
        std::vector<V> g = grid(4, 0.5f, NULL);
        for (V &v : g) { v.p[0] += 0.5f; v.p[1] += 0.5f; }
        draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_NORMAL, g.data(), (int)g.size());
        tss(c, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        tss(c, 0, D3DTSS_TEXCOORDINDEX, 0);
    }
    /* Bottom left: two-weight vertex blending of the whole grid, cut to
     * the quadrant by the scissor rectangle. */
    swvp(c, (sw >> 2) & 1);
    {
        select_diffuse(c);
        RECT sc = {0, 32, 32, 64};
        CK(c.dev->SetScissorRect(&sc));
        rs(c, D3DRS_SCISSORTESTENABLE, TRUE);
        set_blend_matrices(c, NULL, 3);
        rs(c, D3DRS_VERTEXBLEND, D3DVBF_2WEIGHTS);
        std::vector<V> g = grid(12, 1.5f, blend_weights);
        draw(c, D3DPT_TRIANGLELIST, D3DFVF_XYZB2 | D3DFVF_DIFFUSE, g.data(), (int)g.size());
        rs(c, D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
        rs(c, D3DRS_SCISSORTESTENABLE, FALSE);
        D3DMATRIX id = ident();
        CK(c.dev->SetTransform(D3DTS_WORLD, &id));
    }
    /* Bottom right: points of size 8 with their diffuse colours. */
    swvp(c, (sw >> 3) & 1);
    {
        select_diffuse(c);
        rs(c, D3DRS_POINTSIZE, fbits(8.0f));
        V p[4];
        for (int i = 0; i < 4; i++) {
            float px = (i & 1) ? 56.0f : 40.0f, py = (i >> 1) ? 56.0f : 40.0f;
            p[i] = vtx(px / 32.0f - 1.0f, 1.0f - py / 32.0f, 0.5f);
            p[i].diff = 0xff000000 | (0x50u << (8 * (i % 3))) | 0x202020;
        }
        draw(c, D3DPT_POINTLIST, D3DFVF_XYZ | D3DFVF_DIFFUSE, p, 4);
    }
}
static void mx_features_hw_sw(Ctx &c) { mx_features(c, 0xa); }
static void mx_features_sw_hw(Ctx &c) { mx_features(c, 0x5); }
/* Software processing for indices beyond the hardware's matrix palette,
 * then hardware for a plain quad. */
static void mx_indexed_high(Ctx &c)
{
    swvp(c, TRUE);
    vb_indexed_high(c);
    swvp(c, FALSE);
    rs(c, D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    rs(c, D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    D3DMATRIX id = ident();
    CK(c.dev->SetTransform(D3DTS_WORLD, &id));
    V q[4];
    quad_xyz(q, -0.25f, 0.25f, 0.25f, -0.25f, 0.1f);
    for (int i = 0; i < 4; i++) q[i].diff = 0xffff00ff;
    draw(c, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, q, 4);
}

/* ---- the case table --------------------------------------------------- */

enum { MIXED = 1 };
struct Case {
    const char *name;
    int tol;
    void (*fn)(Ctx &);
    int flags;
};

static const Case cases[] = {
    {"vb_0weights", 4, vb_0weights, 0},
    {"vb_1weight", 4, vb_1weight, 0},
    {"vb_2weights", 4, vb_2weights, 0},
    {"vb_3weights", 4, vb_3weights, 0},
    {"vb_1of3weights", 4, vb_1of3weights, 0},
    {"vb_2weights_lit", 4, vb_2weights_lit, 0},
    {"vb_3weights_lit_persp", 4, vb_3weights_lit_persp, 0},
    {"vb_2weights_texgen", 4, vb_2weights_texgen, 0},
    {"vb_3weights_normal", 4, vb_3weights_normal, 0},
    {"vb_indexed_0w", 4, vb_indexed_0w, 0},
    {"vb_indexed_1w", 4, vb_indexed_1w, 0},
    {"vb_indexed_2w", 4, vb_indexed_2w, 0},
    {"vb_indexed_3w", 4, vb_indexed_3w, 0},
    {"vb_indexed_color", 4, vb_indexed_color, 0},
    {"vb_indexed_lit", 4, vb_indexed_lit, 0},
    {"vb_indexed_texgen", 4, vb_indexed_texgen, 0},
    {"vb_tween_color", 4, vb_tween_color, 0},
    {"vb_tween_lit", 4, vb_tween_lit, 0},
    {"bm_const_xyz", 4, bm_const_xyz, 0},
    {"bm_const_rhw", 4, bm_const_rhw, 0},
    {"bm_varying_xyz", 4, bm_varying_xyz, 0},
    {"bm_varying_rhw", 4, bm_varying_rhw, 0},
    {"bm_matrix", 4, bm_matrix, 0},
    {"bm_x8l8v8u8", 4, bm_x8l8v8u8, 0},
    {"bm_lum_x8l8v8u8", 5, bm_lum_x8l8v8u8, 0},
    {"bm_lum_l6v5u5", 5, bm_lum_l6v5u5, 0},
    {"bm_lum_rhw", 5, bm_lum_rhw, 0},
    {"bm_stage1", 5, bm_stage1, 0},
    {"bm_stage1_lum", 5, bm_stage1_lum, 0},
    {"bm_texgen", 4, bm_texgen, 0},
    {"cm_explicit", 4, cm_explicit, 0},
    {"cm_explicit_rhw", 4, cm_explicit_rhw, 0},
    {"cm_explicit_count3", 4, cm_explicit_count3, 0},
    {"cm_reflect", 4, cm_reflect, 0},
    {"cm_reflect_nolocal", 4, cm_reflect_nolocal, 0},
    {"cm_reflect_count3", 4, cm_reflect_count3, 0},
    {"cm_reflect_persp", 4, cm_reflect_persp, 0},
    {"cm_normal", 4, cm_normal, 0},
    {"cm_normal_count3", 4, cm_normal_count3, 0},
    {"cm_position", 4, cm_position, 0},
    {"vol_explicit", 4, vol_explicit, 0},
    {"vol_campos", 4, vol_campos, 0},
    {"mx_hw_sw", 4, mx_hw_sw, MIXED},
    {"mx_sw_hw", 4, mx_sw_hw, MIXED},
    {"mx_all_sw", 4, mx_all_sw, MIXED},
    {"mx_vb_hw_sw", 4, mx_vb_hw_sw, MIXED},
    {"mx_features_hw_sw", 4, mx_features_hw_sw, MIXED},
    {"mx_features_sw_hw", 4, mx_features_sw_hw, MIXED},
    {"mx_indexed_high", 4, mx_indexed_high, MIXED},
};

/* ---- driver ----------------------------------------------------------- */

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static const DWORD CLEARC = 0x80406080;

/* vp: the D3DCREATE_*_VERTEXPROCESSING flag; sw: with a mixed device,
 * whether to start in software vertex processing. */
static bool run_case(IDirect3D9 *d3d, HWND hwnd, D3DDEVTYPE type, DWORD vp, BOOL sw, const Case &k)
{
    Ctx c;
    memset(&c, 0, sizeof c);
    if (k.flags & MIXED) vp = D3DCREATE_MIXED_VERTEXPROCESSING;
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
    if (vp == D3DCREATE_MIXED_VERTEXPROCESSING) CK(c.dev->SetSoftwareVertexProcessing(sw));
    IDirect3DSurface9 *rt = NULL, *ds = NULL, *sys = NULL;
    CK(c.dev->CreateRenderTarget(W, H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, NULL));
    CK(c.dev->CreateDepthStencilSurface(W, H, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &ds, NULL));
    CK(c.dev->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL));
    if (rt) CK(c.dev->SetRenderTarget(0, rt));
    if (ds) CK(c.dev->SetDepthStencilSurface(ds));
    c.grad = make_tex(c, 256, 256, f_grad, 0);
    c.t0 = make_tex(c, 4, 4, f_solid, 0xb0c08040);
    c.cube = make_cube(c);
    c.vol = make_vol(c);
    D3DMATRIX id = ident();
    set_xf(c, id, id, id);
    CK(c.dev->SetRenderState(D3DRS_LIGHTING, FALSE));
    CK(c.dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
    CK(c.dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    CK(c.dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, CLEARC, 1.0f, 0));
    CK(c.dev->BeginScene());
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
    IDirect3DSurface9 *bb = NULL;
    if (rt && SUCCEEDED(c.dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        c.dev->StretchRect(rt, NULL, bb, NULL, D3DTEXF_NONE);
        bb->Release();
        c.dev->Present(NULL, NULL, NULL, NULL);
    }
    logf("CASE %s tol=%d hr=0x%08lx px=%s\n", k.name, k.tol, (unsigned long)c.hr, px);
    if (c.grad) c.grad->Release();
    if (c.t0) c.t0->Release();
    if (c.cube) c.cube->Release();
    if (c.vol) c.vol->Release();
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
    bool sw = false, mixed = false;
    char logpath[MAX_PATH] = "";
    std::vector<std::string> only;
    for (int i = 1; i < argc; i++) {
        char a[512];
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, a, sizeof a, NULL, NULL);
        if (!strcmp(a, "--ref")) type = D3DDEVTYPE_REF;
        else if (!strcmp(a, "--swvp")) sw = true;
        else if (!strcmp(a, "--mixed")) mixed = true;
        else if (!strcmp(a, "--log") && i + 1 < argc)
            WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, logpath, sizeof logpath, NULL, NULL);
        else if (!strcmp(a, "--only") && i + 1 < argc) {
            WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, a, sizeof a, NULL, NULL);
            only.push_back(a);
        }
    }
    DWORD vp = mixed ? D3DCREATE_MIXED_VERTEXPROCESSING
               : sw  ? D3DCREATE_SOFTWARE_VERTEXPROCESSING
                     : D3DCREATE_HARDWARE_VERTEXPROCESSING;
    if (!logpath[0]) {
        GetModuleFileNameA(NULL, logpath, MAX_PATH);
        char *s = strrchr(logpath, '\\');
        snprintf(s ? s + 1 : logpath, MAX_PATH - (s ? (s + 1 - logpath) : 0), "d3d9_ffp2.txt");
    }
    logf_ = fopen(logpath, "w");
    if (!logf_) return 255;
    logf("d3d9_ffp2 device=%s vp=%s%s arch=%s\n", type == D3DDEVTYPE_REF ? "ref" : "hal", mixed ? "mixed-" : "",
         sw ? "sw" : "hw", sizeof(void *) == 8 ? "x64" : "x86");

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = L"d3d9_ffp2";
    RegisterClassW(&wc);
    RECT r = {0, 0, 256, 256};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"d3d9_ffp2", L"d3d9_ffp2", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { logf("Direct3DCreate9 failed\n"); return 255; }
    D3DCAPS9 caps;
    if (SUCCEEDED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, type, &caps)))
        logf("caps DevCaps=0x%08lx TextureOpCaps=0x%08lx TextureCaps=0x%08lx VertexProcessingCaps=0x%08lx "
             "MaxVertexBlendMatrices=%lu MaxVertexBlendMatrixIndex=%lu MaxTextureBlendStages=%lu "
             "MaxSimultaneousTextures=%lu CubeTextureFilterCaps=0x%08lx VolumeTextureFilterCaps=0x%08lx "
             "VS=%lx PS=%lx\n",
             caps.DevCaps, caps.TextureOpCaps, caps.TextureCaps, caps.VertexProcessingCaps, caps.MaxVertexBlendMatrices,
             caps.MaxVertexBlendMatrixIndex, caps.MaxTextureBlendStages, caps.MaxSimultaneousTextures,
             caps.CubeTextureFilterCaps, caps.VolumeTextureFilterCaps, caps.VertexShaderVersion,
             caps.PixelShaderVersion);
    else
        logf("GetDeviceCaps failed\n");
    static const struct { D3DFORMAT f; const char *n; } fmts[] = {
        {D3DFMT_V8U8, "V8U8"}, {D3DFMT_L6V5U5, "L6V5U5"}, {D3DFMT_X8L8V8U8, "X8L8V8U8"}, {D3DFMT_Q8W8V8U8, "Q8W8V8U8"}};
    for (auto &f : fmts)
        logf("format %s texture=0x%08lx\n", f.n,
             (unsigned long)d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, type, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, f.f));

    int ran = 0, failed = 0;
    for (const Case &k : cases) {
        bool want = only.empty();
        for (auto &o : only) if (strstr(k.name, o.c_str())) want = true;
        if (!want) continue;
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        ran++;
        if (!run_case(d3d, hwnd, type, vp, sw, k)) failed++;
    }
    logf("RESULT ran=%d failed_hr=%d\n", ran, failed);
    d3d->Release();
    fclose(logf_);
    return failed;
}
