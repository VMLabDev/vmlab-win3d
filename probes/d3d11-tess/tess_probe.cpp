/* Tessellation probe: a hull shader whose patch-constant function uses the
 * constructs that the DirectX SDK SubD11 sample compiles to (fork phases
 * with instance counts and dcl_indexrange over ordinary patch constants,
 * relative input control-point reads, SV_PrimitiveID), a domain shader that
 * passes each value through, and a pixel shader that writes it to an
 * R32G32B32A32_FLOAT target. It prints one line per (value, patch) with the
 * read-back value and the value computed on the CPU, so a run on HARDWARE
 * shows exactly which construct is translated wrongly; --warp runs the same
 * on WARP as a reference.
 *
 *   tess_probe.exe [--warp] [--log <file>]
 *
 * Exit 0 when every value matches, 1 otherwise. Built by
 * probes/d3d11-tess/build.sh. */
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
    char buf[1024];
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

#define PATCHES 4
#define GRID 2
#define RT 64

/* Vertex v (patch p = v / 4, corner c = v % 4) carries a = (v, 2v + 1, 3v + 2)
 * and b = (v + 0.5, 100 + v). The hull shader adds 1000 * PrimitiveID to a in
 * its control-point phase. */
static const char hlsl[] =
    "cbuffer C : register(b0) { uint sel; uint3 pad; float4 g[4]; };\n"
    "struct VSO { float3 pos : POS; float3 a : ATTR0; float2 b : ATTR1; };\n"
    "VSO vs(uint v : SV_VertexID) {\n"
    "  VSO o; uint p = v / 4, c = v % 4;\n"
    "  float2 org = float2(p % 2, p / 2) - 1.0;\n"
    "  float2 cr = float2(c == 1 || c == 2, c >= 2);\n"
    "  o.pos = float3(org + cr, 0.5);\n"
    "  o.a = float3(v, 2 * v + 1, 3 * v + 2); o.b = float2(v + 0.5, 100 + v); return o; }\n"
    "struct HSC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor;\n"
    "  float3 t[4] : TANG; float2 uv[4] : UVS; float3 c[4] : CORN; float4 w : WTS; };\n"
    "HSC hsc(InputPatch<VSO, 4> ip, uint pid : SV_PrimitiveID) {\n"
    "  HSC o;\n"
    "  [unroll] for (int k = 0; k < 4; k++) o.e[k] = 1;\n"
    "  o.i[0] = o.i[1] = 1;\n"
    "  [unroll] for (int i = 0; i < 4; i++) { o.t[i] = ip[i].a; o.uv[i] = ip[i].b; }\n"
    "  [unroll] for (int j = 0; j < 4; j++) {\n"
    "    float3 s = 0; for (uint n = 0; n < (pid % 3) + 1; n++) s += ip[(j + n) & 3].a;\n"
    "    o.c[j] = s; }\n"
    "  o.w = float4(pid, pid * 7, g[pid].x, g[pid].y);\n"
    "  return o; }\n"
    "[domain(\"quad\")] [partitioning(\"integer\")] [outputtopology(\"triangle_cw\")]\n"
    "[outputcontrolpoints(4)] [patchconstantfunc(\"hsc\")]\n"
    "VSO hs(InputPatch<VSO, 4> p, uint i : SV_OutputControlPointID, uint pid : SV_PrimitiveID) {\n"
    "  VSO o = p[i]; o.a += pid * 1000; return o; }\n"
    "struct DSO { float4 pos : SV_Position; nointerpolation float4 v : VAL; };\n"
    "[domain(\"quad\")] DSO ds(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<VSO, 4> q) {\n"
    "  DSO o;\n"
    "  float3 p0 = lerp(q[0].pos, q[1].pos, uv.x), p1 = lerp(q[3].pos, q[2].pos, uv.x);\n"
    "  o.pos = float4(lerp(p0, p1, uv.y), 1);\n"
    /* Static indices only: a dynamic index into the patch constants is a
     * different construct (dcl_indexrange vpc), not the one SubD11 uses. */
    "  o.v = c.w;\n"
    "  [branch] switch (sel) {\n"
    "  case 0: o.v = float4(c.t[0], 0); break; case 1: o.v = float4(c.t[1], 0); break;\n"
    "  case 2: o.v = float4(c.t[2], 0); break; case 3: o.v = float4(c.t[3], 0); break;\n"
    "  case 4: o.v = float4(c.uv[0], 0, 0); break; case 5: o.v = float4(c.uv[1], 0, 0); break;\n"
    "  case 6: o.v = float4(c.uv[2], 0, 0); break; case 7: o.v = float4(c.uv[3], 0, 0); break;\n"
    "  case 8: o.v = float4(c.c[0], 0); break; case 9: o.v = float4(c.c[1], 0); break;\n"
    "  case 10: o.v = float4(c.c[2], 0); break; case 11: o.v = float4(c.c[3], 0); break;\n"
    "  case 12: o.v = float4(q[0].a, 0); break; case 13: o.v = float4(q[1].a, 0); break;\n"
    "  case 14: o.v = float4(q[2].a, 0); break; case 15: o.v = float4(q[3].a, 0); break; }\n"
    "  return o; }\n"
    /* sel 17: SubD11's SmoothPS light loop, which reads an immediate
     * constant buffer (dcl_immediateConstantBuffer) with a dynamic index. */
    "static const float3 D[4] = { float3(-0.701513, -0.642805, 0.307705), float3(0.379523, -0.279064, 0.882092),\n"
    "  float3(0.447078, -0.501002, -0.741025), float3(-0.543092, 0.424649, -0.724379) };\n"
    "static const float3 L[4] = { float3(1, 0.922507, 0.464912), float3(0.169253, 0.375913, 1),\n"
    "  float3(1, 0.7213, 0.147527), float3(0.136899, 0.361867, 1) };\n"
    "float4 ps(DSO i) : SV_Target {\n"
    "  if (sel != 17) return i.v;\n"
    "  float3 n = normalize(float3(i.v.x - 1.5, 0.5, 1)); float3 acc = 0;\n"
    "  [loop] for (int k = 0; k < 4; k++) { float d = saturate(dot(n, -D[k])); acc += L[k] * d * d; }\n"
    "  return float4(acc, 1); }\n";

typedef HRESULT(WINAPI *d3dcompile_fn)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
                                       ID3DInclude *, const char *, const char *, UINT, UINT,
                                       ID3DBlob **, ID3DBlob **);

static ID3DBlob *compile(const char *entry, const char *target)
{
    static d3dcompile_fn fn;
    if (!fn) {
        HMODULE m = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        fn = m ? (d3dcompile_fn)(void *)GetProcAddress(m, "D3DCompile") : NULL;
        if (!fn) {
            say("cannot load D3DCompile from d3dcompiler_47.dll");
            return NULL;
        }
    }
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = fn(hlsl, sizeof(hlsl) - 1, "tess_probe", NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr))
        say("D3DCompile %s %s: 0x%08lx %s", entry, target, (unsigned long)hr,
            err ? (const char *)err->GetBufferPointer() : "");
    if (err)
        err->Release();
    return code;
}

/* What each value should be for patch p (sel as in the domain shader). */
static void expected(unsigned sel, unsigned p, float *e)
{
    const unsigned k = sel % 4;
    memset(e, 0, 4 * sizeof(float));
    if (sel < 4 || (sel >= 12 && sel < 16)) {
        unsigned v = p * 4 + k;
        e[0] = (float)v;
        e[1] = (float)(2 * v + 1);
        e[2] = (float)(3 * v + 2);
        if (sel >= 12) {
            e[0] += 1000.0f * p;
            e[1] += 1000.0f * p;
            e[2] += 1000.0f * p;
        }
    } else if (sel < 8) {
        unsigned v = p * 4 + k;
        e[0] = v + 0.5f;
        e[1] = 100.0f + v;
    } else if (sel < 12) {
        for (unsigned n = 0; n < (p % 3) + 1; n++) {
            unsigned v = p * 4 + ((k + n) & 3);
            e[0] += (float)v;
            e[1] += (float)(2 * v + 1);
            e[2] += (float)(3 * v + 2);
        }
    } else if (sel == 17) {
        static const float D[4][3] = {{-0.701513f, -0.642805f, 0.307705f}, {0.379523f, -0.279064f, 0.882092f},
                                      {0.447078f, -0.501002f, -0.741025f}, {-0.543092f, 0.424649f, -0.724379f}};
        static const float L[4][3] = {{1, 0.922507f, 0.464912f}, {0.169253f, 0.375913f, 1},
                                      {1, 0.7213f, 0.147527f}, {0.136899f, 0.361867f, 1}};
        float n[3] = {p - 1.5f, 0.5f, 1.0f};
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int c = 0; c < 3; c++)
            n[c] /= len;
        for (int k = 0; k < 4; k++) {
            float d = -(n[0] * D[k][0] + n[1] * D[k][1] + n[2] * D[k][2]);
            d = d < 0 ? 0 : d > 1 ? 1 : d;
            for (int c = 0; c < 3; c++)
                e[c] += L[k][c] * d * d;
        }
        e[3] = 1;
    } else {
        e[0] = (float)p;
        e[1] = 7.0f * p;
        e[2] = 10.0f + p;
        e[3] = 20.0f + p;
    }
}

static const char *sel_name(unsigned sel)
{
    static const char *n[] = {"t (indexed fork-phase output, vicp[i][a])",
                              "uv (indexed fork-phase output, vicp[i][b])",
                              "c (loop over vicp[(j + n) & 3], PrimitiveID)",
                              "q[k].a (control-point output, PrimitiveID)",
                              "w (PrimitiveID, cbuffer indexed by it)",
                              "pixel-shader loop over an immediate constant buffer"};
    return n[sel < 16 ? sel / 4 : sel == 16 ? 4 : 5];
}

int wmain(int argc, wchar_t **argv)
{
    D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_HARDWARE;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--warp"))
            type = D3D_DRIVER_TYPE_WARP;
        else if (!wcscmp(argv[i], L"--log") && i + 1 < argc)
            g_log = _wfopen(argv[++i], L"w");
    }
    say("tess_probe: driver type %s", type == D3D_DRIVER_TYPE_WARP ? "WARP" : "HARDWARE");

    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *dc = NULL;
    HRESULT hr = D3D11CreateDevice(NULL, type, NULL, 0, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &dc);
    if (FAILED(hr)) {
        say("D3D11CreateDevice: 0x%08lx", (unsigned long)hr);
        return 1;
    }

    ID3DBlob *bvs = compile("vs", "vs_5_0"), *bhs = compile("hs", "hs_5_0"),
             *bds = compile("ds", "ds_5_0"), *bps = compile("ps", "ps_5_0");
    if (!bvs || !bhs || !bds || !bps)
        return 1;
    ID3D11VertexShader *vs = NULL;
    ID3D11HullShader *hs = NULL;
    ID3D11DomainShader *ds = NULL;
    ID3D11PixelShader *ps = NULL;
    hr = dev->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(), NULL, &vs);
    say("CreateVertexShader 0x%08lx", (unsigned long)hr);
    hr = dev->CreateHullShader(bhs->GetBufferPointer(), bhs->GetBufferSize(), NULL, &hs);
    say("CreateHullShader 0x%08lx", (unsigned long)hr);
    hr = dev->CreateDomainShader(bds->GetBufferPointer(), bds->GetBufferSize(), NULL, &ds);
    say("CreateDomainShader 0x%08lx", (unsigned long)hr);
    hr = dev->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(), NULL, &ps);
    say("CreatePixelShader 0x%08lx", (unsigned long)hr);
    if (!vs || !hs || !ds || !ps)
        return 1;
    say("device removed reason: 0x%08lx", (unsigned long)dev->GetDeviceRemovedReason());

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

    struct {
        UINT sel, pad[3];
        float g[4][4];
    } cb = {};
    for (int p = 0; p < 4; p++) {
        cb.g[p][0] = 10.0f + p;
        cb.g[p][1] = 20.0f + p;
    }
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
    for (UINT sel = 0; sel < 18; sel++) {
        cb.sel = sel;
        dc->UpdateSubresource(cbuf, 0, NULL, &cb, 0, 0);
        const float clear[4] = {-1, -1, -1, -1};
        dc->ClearRenderTargetView(rtv, clear);
        dc->OMSetRenderTargets(1, &rtv, NULL);
        D3D11_VIEWPORT vp = {0, 0, RT, RT, 0, 1};
        dc->RSSetViewports(1, &vp);
        dc->RSSetState(rs);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
        dc->VSSetShader(vs, NULL, 0);
        dc->HSSetShader(hs, NULL, 0);
        dc->DSSetShader(ds, NULL, 0);
        dc->PSSetShader(ps, NULL, 0);
        dc->HSSetConstantBuffers(0, 1, &cbuf);
        dc->DSSetConstantBuffers(0, 1, &cbuf);
        dc->PSSetConstantBuffers(0, 1, &cbuf);
        dc->Draw(PATCHES * 4, 0);
        dc->CopyResource(staging, rt);
        D3D11_MAPPED_SUBRESOURCE m;
        hr = dc->Map(staging, 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) {
            say("Map: 0x%08lx", (unsigned long)hr);
            return 1;
        }
        for (unsigned p = 0; p < PATCHES; p++) {
            /* Patch p covers clip-space quadrant (p % 2, p / 2) - 1 .. +1:
             * x left to right, y bottom to top. */
            unsigned px = (p % GRID) * (RT / 2) + RT / 4;
            unsigned py = (GRID - 1 - p / GRID) * (RT / 2) + RT / 4;
            const float *v = (const float *)((const char *)m.pData + py * m.RowPitch) + px * 4;
            float e[4];
            expected(sel, p, e);
            int ok = 1;
            for (int c = 0; c < 4; c++)
                if (fabsf(v[c] - e[c]) > 2e-3f)
                    ok = 0;
            bad += !ok;
            say("sel %2u patch %u: got (%g, %g, %g, %g) want (%g, %g, %g, %g) %s  [%s, k=%u]", sel, p,
                v[0], v[1], v[2], v[3], e[0], e[1], e[2], e[3], ok ? "ok" : "MISMATCH", sel_name(sel),
                sel % 4);
        }
        dc->Unmap(staging, 0);
    }
    say("device removed reason: 0x%08lx", (unsigned long)dev->GetDeviceRemovedReason());
    say("%d mismatches", bad);
    if (g_log)
        fclose(g_log);
    return bad ? 1 : 0;
}
