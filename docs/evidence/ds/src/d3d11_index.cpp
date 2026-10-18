/* Repro for dynamically indexed shader inputs and outputs (docs/ds.md).
 *
 * Each case draws four patches or quads, one per quadrant of a 64x64
 * R32G32B32A32_FLOAT target, with shaders whose array input is indexed by a
 * register: the index is (quadrant + sel) & 3, with sel from a
 * constant buffer, so the compiler cannot fold it. d3dcompiler_47 turns those
 * accesses into dcl_indexrange declarations; the disassembly lines that show
 * them are printed with each case. The centre pixel of each quadrant is read
 * back and compared with the value computed on the CPU, for sel 0 to 3.
 *
 *   d3d11_index.exe [--warp] [--case NAME] [--asm DIR] [--log FILE]
 *
 * Every case gets its own device, so one that removes the device does not
 * hide the others. Exit 0 when every case matches, 1 otherwise. Run once on
 * HARDWARE and once with --warp; both must print the same values.
 * Built by docs/evidence/ds/src/build.sh. */
#include <windows.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *g_log;

static void say(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    fflush(stdout);
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
}

#define RT 64

/* val(p, c, k): the value of array element k at corner c of quadrant p. */
static const char common_hlsl[] =
    "cbuffer C : register(b0) { uint sel; uint3 pad; };\n"
    "float4 val(uint p, uint c, uint k) {\n"
    "  return float4(1000 * c + 100 * p + 10 * k + 1, 1000 * c + 100 * p + 10 * k + 2,\n"
    "                1000 * c + 100 * p + 10 * k + 3, 1000 * c + 100 * p + 10 * k + 4); }\n"
    "uint idx(uint p) { return (p + sel) & 3; }\n"
    /* Quadrant p as a 4-control-point patch, corner c (tessellated cases). */
    "float3 patch_pos(uint p, uint c) {\n"
    "  float2 org = float2(p % 2, p / 2) - 1.0;\n"
    "  return float3(org + float2(c == 1 || c == 2, c >= 2), 0.5); }\n"
    /* Quadrant p as two triangles, vertex v of 6 (other cases). */
    "float4 quad_pos(uint p, uint v) {\n"
    "  static const float2 k[6] = { float2(0, 0), float2(1, 0), float2(0, 1),\n"
    "                               float2(1, 0), float2(1, 1), float2(0, 1) };\n"
    "  return float4(float2(p % 2, p / 2) - 1.0 + k[v], 0.5, 1); }\n"
    /* Tessellated pipeline: per-control-point array a[2], patch-constant
     * array c[4] after the six quad tessellation factors (vpc6..vpc9). */
    "struct CP { float3 pos : POS; nointerpolation uint p : QUAD; float4 a[2] : ATTR; };\n"
    "CP vs_tess(uint v : SV_VertexID) {\n"
    "  CP o; uint p = v / 4, c = v % 4;\n"
    "  o.pos = patch_pos(p, c); o.p = p; o.a[0] = val(p, c, 0); o.a[1] = val(p, c, 1); return o; }\n"
    "struct HSC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor;\n"
    "  float3 c[4] : CONSTS; };\n"
    "HSC hsc(InputPatch<CP, 4> ip) {\n"
    "  HSC o;\n"
    "  [unroll] for (int k = 0; k < 4; k++) { o.e[k] = 1; o.c[k] = val(ip[0].p, 9, k).xyz; }\n"
    "  o.i[0] = o.i[1] = 1; return o; }\n"
    "[domain(\"quad\")] [partitioning(\"integer\")] [outputtopology(\"triangle_cw\")]\n"
    "[outputcontrolpoints(4)] [patchconstantfunc(\"hsc\")]\n"
    "CP hs(InputPatch<CP, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n"
    "float4 ds_pos(float2 uv, const OutputPatch<CP, 4> q) {\n"
    "  float3 p0 = lerp(q[0].pos, q[1].pos, uv.x), p1 = lerp(q[3].pos, q[2].pos, uv.x);\n"
    "  return float4(lerp(p0, p1, uv.y), 1); }\n"
    "struct O1 { float4 pos : SV_Position; nointerpolation float4 v : VAL; };\n"
    "float4 ps_one(O1 i) : SV_Target { return i.v; }\n"
    /* Untessellated pipeline: array a[4] per vertex. */
    "struct V4 { float4 pos : SV_Position; nointerpolation uint p : QUAD;\n"
    "  nointerpolation float4 a[4] : ATTR; };\n"
    "V4 vs_quad(uint v : SV_VertexID) {\n"
    "  V4 o; uint p = v / 6, c = v % 3;\n"
    "  o.pos = quad_pos(p, v % 6); o.p = p;\n"
    "  [unroll] for (int k = 0; k < 4; k++) o.a[k] = val(p, c, k); return o; }\n";

struct Case {
    const char *name;
    const char *what;
    bool tess;
    const char *vs, *hs, *ds, *gs, *ps; /* entry points; vs/ps always set */
    const char *hlsl;                   /* appended to common_hlsl */
    /* Expected value for quadrant p at this sel. */
    void (*expect)(unsigned p, unsigned sel, float *e);
};

static void val(unsigned p, unsigned c, unsigned k, float scale, float *e)
{
    for (int i = 0; i < 4; i++)
        e[i] = scale * (1000.0f * c + 100.0f * p + 10.0f * k + i + 1);
}
static unsigned idx(unsigned p, unsigned sel) { return (p + sel) & 3; }

static void e_ds_vpc(unsigned p, unsigned sel, float *e)
{
    val(p, 9, idx(p, sel), 1, e);
    e[3] = 1;
}
static void e_ds_vcp(unsigned p, unsigned sel, float *e) { val(p, idx(p, sel), (p + sel) & 1, 1, e); }
static void e_ds_loop(unsigned p, unsigned sel, float *e)
{
    float t[4];
    e[0] = e[1] = e[2] = 0;
    for (unsigned n = 0; n < (p + sel) % 3 + 1; n++) {
        val(p, 9, n, 1, t);
        for (int i = 0; i < 3; i++)
            e[i] += t[i];
    }
    e[3] = 1;
}
static void e_gs_in(unsigned p, unsigned sel, float *e) { val(p, (p + sel) % 3, idx(p, sel), 1, e); }
static void e_ps_in(unsigned p, unsigned sel, float *e) { val(p, 0, idx(p, sel), 1, e); }

static const Case cases[] = {
    {"ds-vpc", "domain shader reads patch constant c[idx] (vpc range)", true,
     "vs_tess", "hs", "ds", NULL, "ps_one",
     "[domain(\"quad\")] O1 ds(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n"
     "  O1 o; o.pos = ds_pos(uv, q); o.v = float4(c.c[idx(q[0].p)], 1); return o; }\n",
     e_ds_vpc},
    {"ds-vcp", "domain shader reads control point q[idx].a[j] (vcp, both indices dynamic)", true,
     "vs_tess", "hs", "ds", NULL, "ps_one",
     "[domain(\"quad\")] O1 ds(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n"
     "  O1 o; o.pos = ds_pos(uv, q); uint p = q[0].p;\n"
     "  o.v = q[idx(p)].a[(p + sel) & 1]; return o; }\n",
     e_ds_vcp},
    {"ds-vpc-loop", "domain shader sums c[n] in a loop (vpc range, loop counter index)", true,
     "vs_tess", "hs", "ds", NULL, "ps_one",
     "[domain(\"quad\")] O1 ds(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n"
     "  O1 o; o.pos = ds_pos(uv, q); uint p = q[0].p; float3 s = 0;\n"
     "  [loop] for (uint n = 0; n < (p + sel) % 3 + 1; n++) s += c.c[n];\n"
     "  o.v = float4(s, 1); return o; }\n",
     e_ds_loop},
    {"gs-in", "geometry shader reads t[vertex].a[idx] (v range, both indices dynamic)", false,
     "vs_quad", NULL, NULL, "gs", "ps_one",
     "[maxvertexcount(3)] void gs(triangle V4 t[3], inout TriangleStream<O1> s) {\n"
     "  uint p = t[0].p; float4 v = t[(p + sel) % 3].a[idx(p)];\n"
     "  [unroll] for (int i = 0; i < 3; i++) { O1 o; o.pos = t[i].pos; o.v = v; s.Append(o); } }\n",
     e_gs_in},
    {"ps-in", "pixel shader reads input a[idx] (v range)", false,
     "vs_quad", NULL, NULL, NULL, "ps",
     "float4 ps(V4 i) : SV_Target { return i.a[idx(i.p)]; }\n",
     e_ps_in},
};

typedef HRESULT(WINAPI *d3dcompile_fn)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
                                       ID3DInclude *, const char *, const char *, UINT, UINT,
                                       ID3DBlob **, ID3DBlob **);
typedef HRESULT(WINAPI *d3ddisasm_fn)(const void *, SIZE_T, UINT, const char *, ID3DBlob **);
static d3dcompile_fn p_compile;
static d3ddisasm_fn p_disasm;
static const wchar_t *g_asm_dir;

static ID3DBlob *compile(const Case &c, const char *entry, const char *target)
{
    static char src[16384];
    snprintf(src, sizeof(src), "%s%s", common_hlsl, c.hlsl);
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = p_compile(src, strlen(src), c.name, NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr))
        say("  D3DCompile %s %s: 0x%08lx %s", entry, target, (unsigned long)hr,
            err ? (const char *)err->GetBufferPointer() : "");
    if (err)
        err->Release();
    if (!code)
        return NULL;

    /* Show the indexed declarations and accesses this case exists for. */
    ID3DBlob *text = NULL;
    if (p_disasm && SUCCEEDED(p_disasm(code->GetBufferPointer(), code->GetBufferSize(), 0, NULL, &text))) {
        const char *s = (const char *)text->GetBufferPointer();
        if (g_asm_dir) {
            wchar_t path[MAX_PATH];
            swprintf(path, MAX_PATH, L"%ls\\%hs-%hs.asm.txt", g_asm_dir, c.name, target);
            FILE *f = _wfopen(path, L"w");
            if (f) {
                fputs(s, f);
                fclose(f);
            }
        }
        while (*s) {
            const char *e = strchr(s, '\n');
            size_t n = e ? (size_t)(e - s) : strlen(s);
            char line[512];
            snprintf(line, sizeof(line), "%.*s", (int)(n < 500 ? n : 500), s);
            if (strstr(line, "indexrange") || strstr(line, "[r") || strstr(line, "+ r"))
                say("  %s: %s", target, line + strspn(line, " "));
            s += n + (e ? 1 : 0);
        }
        text->Release();
    }
    return code;
}

/* Returns the number of mismatching values, or -1 when the case could not run. */
static int run_case(const Case &c, D3D_DRIVER_TYPE type)
{
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *dc = NULL;
    HRESULT hr = D3D11CreateDevice(NULL, type, NULL, 0, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &dc);
    if (FAILED(hr)) {
        say("  D3D11CreateDevice: 0x%08lx", (unsigned long)hr);
        return -1;
    }

    ID3D11VertexShader *vs = NULL;
    ID3D11HullShader *hs = NULL;
    ID3D11DomainShader *ds = NULL;
    ID3D11GeometryShader *gs = NULL;
    ID3D11PixelShader *ps = NULL;
    bool ok = true;
    ID3DBlob *b;
#define CREATE(stage, Kind, var, target)                                                          \
    if (c.stage) {                                                                                \
        b = compile(c, c.stage, target);                                                          \
        if (!b) {                                                                                 \
            ok = false;                                                                           \
        } else {                                                                                  \
            hr = dev->Create##Kind##Shader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &var); \
            say("  Create" #Kind "Shader 0x%08lx", (unsigned long)hr);                             \
            ok = ok && SUCCEEDED(hr);                                                             \
            b->Release();                                                                         \
        }                                                                                         \
    }
    CREATE(vs, Vertex, vs, "vs_5_0")
    CREATE(hs, Hull, hs, "hs_5_0")
    CREATE(ds, Domain, ds, "ds_5_0")
    CREATE(gs, Geometry, gs, "gs_5_0")
    CREATE(ps, Pixel, ps, "ps_5_0")
#undef CREATE
    hr = dev->GetDeviceRemovedReason();
    if (FAILED(hr)) {
        say("  device removed: 0x%08lx", (unsigned long)hr);
        ok = false;
    }
    if (!ok) {
        dc->Release();
        dev->Release();
        return -1;
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = RT;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = NULL, *staging = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    dev->CreateTexture2D(&td, NULL, &rt);
    dev->CreateRenderTargetView(rt, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, NULL, &staging);

    UINT cb[4] = {};
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(cb);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer *cbuf = NULL;
    dev->CreateBuffer(&bd, NULL, &cbuf);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState *rs = NULL;
    dev->CreateRasterizerState(&rd, &rs);

    int bad = 0;
    for (UINT sel = 0; sel < 4; sel++) {
        cb[0] = sel;
        dc->UpdateSubresource(cbuf, 0, NULL, cb, 0, 0);
        const float clear[4] = {-1, -1, -1, -1};
        dc->ClearRenderTargetView(rtv, clear);
        dc->OMSetRenderTargets(1, &rtv, NULL);
        D3D11_VIEWPORT vp = {0, 0, RT, RT, 0, 1};
        dc->RSSetViewports(1, &vp);
        dc->RSSetState(rs);
        dc->IASetPrimitiveTopology(c.tess ? D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
                                          : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->VSSetShader(vs, NULL, 0);
        dc->HSSetShader(hs, NULL, 0);
        dc->DSSetShader(ds, NULL, 0);
        dc->GSSetShader(gs, NULL, 0);
        dc->PSSetShader(ps, NULL, 0);
        dc->VSSetConstantBuffers(0, 1, &cbuf);
        dc->HSSetConstantBuffers(0, 1, &cbuf);
        dc->DSSetConstantBuffers(0, 1, &cbuf);
        dc->GSSetConstantBuffers(0, 1, &cbuf);
        dc->PSSetConstantBuffers(0, 1, &cbuf);
        dc->Draw(c.tess ? 16 : 24, 0);
        dc->CopyResource(staging, rt);
        D3D11_MAPPED_SUBRESOURCE m;
        hr = dc->Map(staging, 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) {
            say("  Map: 0x%08lx", (unsigned long)hr);
            bad = -1;
            break;
        }
        for (unsigned p = 0; p < 4; p++) {
            /* Quadrant p spans clip x (p % 2) - 1 .. (p % 2) and y (p / 2) - 1 .. (p / 2);
             * clip y points up, rows go down. */
            unsigned px = (p % 2) * (RT / 2) + RT / 4;
            unsigned py = (1 - p / 2) * (RT / 2) + RT / 4;
            const float *v = (const float *)((const char *)m.pData + py * m.RowPitch) + px * 4;
            float e[4];
            c.expect(p, sel, e);
            bool match = true;
            for (int k = 0; k < 4; k++)
                if (fabsf(v[k] - e[k]) > 1e-3f * (1 + fabsf(e[k])))
                    match = false;
            bad += !match;
            say("  sel %u quad %u: got (%g, %g, %g, %g) want (%g, %g, %g, %g) %s", sel, p, v[0], v[1],
                v[2], v[3], e[0], e[1], e[2], e[3], match ? "ok" : "MISMATCH");
        }
        dc->Unmap(staging, 0);
    }
    hr = dev->GetDeviceRemovedReason();
    if (FAILED(hr)) {
        say("  device removed: 0x%08lx", (unsigned long)hr);
        bad = -1;
    }
    dc->ClearState();
    rs->Release();
    cbuf->Release();
    staging->Release();
    rtv->Release();
    rt->Release();
    if (vs) vs->Release();
    if (hs) hs->Release();
    if (ds) ds->Release();
    if (gs) gs->Release();
    if (ps) ps->Release();
    dc->Release();
    dev->Release();
    return bad;
}

int wmain(int argc, wchar_t **argv)
{
    D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_HARDWARE;
    const wchar_t *only = NULL;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--warp"))
            type = D3D_DRIVER_TYPE_WARP;
        else if (!wcscmp(argv[i], L"--case") && i + 1 < argc)
            only = argv[++i];
        else if (!wcscmp(argv[i], L"--asm") && i + 1 < argc)
            g_asm_dir = argv[++i];
        else if (!wcscmp(argv[i], L"--log") && i + 1 < argc)
            g_log = _wfopen(argv[++i], L"w");
    }
    say("d3d11_index: driver type %s, %u-bit", type == D3D_DRIVER_TYPE_WARP ? "WARP" : "HARDWARE",
        (unsigned)(sizeof(void *) * 8));

    HMODULE m = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    p_compile = m ? (d3dcompile_fn)(void *)GetProcAddress(m, "D3DCompile") : NULL;
    p_disasm = m ? (d3ddisasm_fn)(void *)GetProcAddress(m, "D3DDisassemble") : NULL;
    if (!p_compile) {
        say("cannot load D3DCompile from d3dcompiler_47.dll");
        return 1;
    }

    int failed = 0, ran = 0;
    for (const Case &c : cases) {
        wchar_t wname[64];
        swprintf(wname, 64, L"%hs", c.name);
        if (only && wcscmp(only, wname))
            continue;
        say("case %s: %s", c.name, c.what);
        int bad = run_case(c, type);
        ran++;
        if (bad)
            failed++;
        say("RESULT %s %s", c.name, bad < 0 ? "FAIL (could not run)" : bad ? "FAIL (mismatch)" : "PASS");
    }
    say("%d of %d cases pass", ran - failed, ran);
    if (g_log)
        fclose(g_log);
    return failed ? 1 : 0;
}
