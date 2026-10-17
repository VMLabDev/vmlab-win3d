/* Direct3D 11 probe: D3D11CreateDevice on the default adapter (hardware driver
 * type, or WARP with --warp) at feature level 11_0 down to 10_0, a DXGI
 * swap chain on the window, vs_4_0/ps_4_0 shaders, staging-copy readback. */
#include "../common/probe.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/dxgi_util.h"

static const char hlsl[] =
    "struct VI { float2 pos : POSITION; float4 col : COLOR; };\n"
    "struct VO { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "VO vs(VI i) { VO o; o.pos = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }\n"
    "float4 ps(VO i) : SV_Target { return i.col; }\n";

static const char *fl_name(D3D_FEATURE_LEVEL fl)
{
    switch (fl) {
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    default: return "other";
    }
}

static int run(probe_ctx *ctx)
{
    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_DRIVER_TYPE type = ctx->warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *dc = NULL;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(NULL, type, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &dev, &fl, &dc);
    probe_str(ctx, PROBE_API, "driver_type", ctx->warp ? "WARP" : "HARDWARE");
    if (probe_hr(ctx, hr, ctx->warp ? "D3D11CreateDevice(WARP)" : "D3D11CreateDevice(HARDWARE)"))
        return 1;
    probe_str(ctx, PROBE_API, "feature_level", fl_name(fl));

    IDXGIDevice *dxgi_dev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory1 *factory = NULL;
    if (probe_hr(ctx, dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev),
                 "QueryInterface(IDXGIDevice)") ||
        probe_hr(ctx, dxgi_dev->GetAdapter(&adapter), "IDXGIDevice::GetAdapter") ||
        probe_hr(ctx, adapter->GetParent(__uuidof(IDXGIFactory1), (void **)&factory),
                 "IDXGIAdapter::GetParent(IDXGIFactory1)"))
        return 1;
    describe_adapter(ctx, adapter, ctx->warp);

    /* Flip model first (Windows 10+), then the legacy blit model. */
    IDXGISwapChain *swap = NULL;
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = (UINT)ctx->width;
    sd.BufferDesc.Height = (UINT)ctx->height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = ctx->hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = factory->CreateSwapChain(dev, &sd, &swap);
    if (FAILED(hr)) {
        sd.BufferCount = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = factory->CreateSwapChain(dev, &sd, &swap);
    }
    if (probe_hr(ctx, hr, "IDXGIFactory::CreateSwapChain"))
        return 1;
    probe_str(ctx, PROBE_API, "swap_effect",
              sd.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ? "FLIP_DISCARD" : "DISCARD");
    factory->MakeWindowAssociation(ctx->hwnd, DXGI_MWA_NO_ALT_ENTER);

    size_t vs_size = 0, ps_size = 0;
    void *vs_code = probe_compile_hlsl(ctx, hlsl, "vs", "vs_4_0", &vs_size);
    void *ps_code = probe_compile_hlsl(ctx, hlsl, "ps", "ps_4_0", &ps_size);
    if (!vs_code || !ps_code)
        return 1;
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11InputLayout *layout = NULL;
    static const D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (probe_hr(ctx, dev->CreateVertexShader(vs_code, vs_size, NULL, &vs), "CreateVertexShader") ||
        probe_hr(ctx, dev->CreatePixelShader(ps_code, ps_size, NULL, &ps), "CreatePixelShader") ||
        probe_hr(ctx, dev->CreateInputLayout(elems, 2, vs_code, vs_size, &layout), "CreateInputLayout"))
        return 1;
    free(vs_code);
    free(ps_code);

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = PROBE_MAX_VERTICES * sizeof(probe_vertex);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer *vb = NULL;
    if (probe_hr(ctx, dev->CreateBuffer(&bd, NULL, &vb), "CreateBuffer(vertex)"))
        return 1;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState *rs = NULL;
    if (probe_hr(ctx, dev->CreateRasterizerState(&rd, &rs), "CreateRasterizerState"))
        return 1;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)ctx->width;
    td.Height = (UINT)ctx->height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging = NULL;
    if (probe_hr(ctx, dev->CreateTexture2D(&td, NULL, &staging), "CreateTexture2D(staging)"))
        return 1;

    D3D11_VIEWPORT vp = {0, 0, (float)ctx->width, (float)ctx->height, 0, 1};
    UINT stride = sizeof(probe_vertex), offset = 0;
    for (;;) {
        probe_pump(ctx);
        int last = probe_last_frame(ctx);

        /* Flip-model buffers rotate, so look up buffer 0 every frame. */
        ID3D11Texture2D *bb = NULL;
        ID3D11RenderTargetView *rtv = NULL;
        if (probe_hr(ctx, swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb), "GetBuffer") ||
            probe_hr(ctx, dev->CreateRenderTargetView(bb, NULL, &rtv), "CreateRenderTargetView"))
            return 1;

        D3D11_MAPPED_SUBRESOURCE ms;
        if (probe_hr(ctx, dc->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms), "Map(vertex)"))
            return 1;
        int n = probe_build_scene(ctx, probe_now(ctx), (probe_vertex *)ms.pData, PROBE_MAX_VERTICES);
        dc->Unmap(vb, 0);

        dc->OMSetRenderTargets(1, &rtv, NULL);
        dc->ClearRenderTargetView(rtv, probe_clear_color);
        dc->RSSetViewports(1, &vp);
        dc->RSSetState(rs);
        dc->IASetInputLayout(layout);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        dc->VSSetShader(vs, NULL, 0);
        dc->PSSetShader(ps, NULL, 0);
        dc->Draw((UINT)n, 0);

        if (last) {
            dc->CopyResource(staging, bb);
            if (probe_hr(ctx, dc->Map(staging, 0, D3D11_MAP_READ, 0, &ms), "Map(staging)"))
                return 1;
            probe_readback_image(ctx, (const uint8_t *)ms.pData, ms.RowPitch, 0, 0);
            dc->Unmap(staging, 0);
        }
        rtv->Release();
        bb->Release();

        hr = swap->Present(1, 0);
        if (hr == DXGI_STATUS_OCCLUDED) {
            /* The window is hidden; not a failure. */
        } else if (probe_hr(ctx, hr, "IDXGISwapChain::Present")) {
            return 1;
        } else {
            probe_frame_presented(ctx);
        }
        if (last)
            break;
    }
    staging->Release();
    rs->Release();
    vb->Release();
    layout->Release();
    ps->Release();
    vs->Release();
    swap->Release();
    factory->Release();
    adapter->Release();
    dxgi_dev->Release();
    dc->Release();
    dev->Release();
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    probe_ctx ctx;
    int rc = probe_init(&ctx, argc, argv, "d3d11", "Direct3D 11", "DIRECT3D 11", PROBE_ALLOW_WARP);
    if (rc)
        return rc;
    if (ctx.hwnd)
        run(&ctx);
    return probe_finish(&ctx);
}
