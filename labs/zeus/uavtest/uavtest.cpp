// D3D11 typed UAV load test for docs/zeus.md (patches/mesa/0021).
// Creates a hardware FL11_0 device with no window, prints the typed UAV load
// caps, then runs a compute shader that reads and writes an R32_FLOAT and an
// R32_UINT RWTexture2D (t = t * 2 + 1) and checks the result on the CPU.
// Exit 0 when both pass. Runs from any session (no window).
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

static const char *kShader[2] = {
    "RWTexture2D<float> t : register(u0);\n"
    "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID)\n"
    "{ t[id.xy] = t[id.xy] * 2.0 + 1.0; }\n",
    "RWTexture2D<uint> t : register(u0);\n"
    "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID)\n"
    "{ t[id.xy] = t[id.xy] * 2 + 1; }\n",
};

static bool run(ID3D11Device *dev, ID3D11DeviceContext *ctx, int kind)
{
    const DXGI_FORMAT fmt = kind ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R32_FLOAT;
    const char *name = kind ? "R32_UINT" : "R32_FLOAT";
    const UINT W = 64, H = 64;
    static unsigned init[64 * 64];
    for (UINT i = 0; i < W * H; i++) {
        if (kind) init[i] = i;
        else { float f = (float)i; memcpy(&init[i], &f, 4); }
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    D3D11_SUBRESOURCE_DATA sd = {init, W * 4, 0};
    ID3D11Texture2D *tex = nullptr, *staging = nullptr;
    ID3D11UnorderedAccessView *uav = nullptr;
    ID3D11ComputeShader *cs = nullptr;
    ID3DBlob *code = nullptr, *err = nullptr;
    bool ok = false;
    HRESULT hr = dev->CreateTexture2D(&td, &sd, &tex);
    if (FAILED(hr)) { printf("%s: CreateTexture2D 0x%08lx\n", name, hr); goto out; }
    hr = dev->CreateUnorderedAccessView(tex, nullptr, &uav);
    if (FAILED(hr)) { printf("%s: CreateUnorderedAccessView 0x%08lx\n", name, hr); goto out; }
    hr = D3DCompile(kShader[kind], strlen(kShader[kind]), "uavtest", nullptr, nullptr,
                    "main", "cs_5_0", 0, 0, &code, &err);
    if (FAILED(hr)) { printf("%s: D3DCompile 0x%08lx %s\n", name, hr, err ? (char *)err->GetBufferPointer() : ""); goto out; }
    hr = dev->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &cs);
    if (FAILED(hr)) { printf("%s: CreateComputeShader 0x%08lx\n", name, hr); goto out; }
    ctx->CSSetShader(cs, nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    ctx->Dispatch(W / 8, H / 8, 1);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = dev->CreateTexture2D(&td, nullptr, &staging);
    if (FAILED(hr)) { printf("%s: staging 0x%08lx\n", name, hr); goto out; }
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE m;
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) { printf("%s: Map 0x%08lx\n", name, hr); goto out; }
    {
        UINT bad = 0, first = ~0u;
        for (UINT y = 0; y < H; y++)
            for (UINT x = 0; x < W; x++) {
                const unsigned *row = (const unsigned *)((const char *)m.pData + y * m.RowPitch);
                const UINT i = y * W + x;
                bool good;
                if (kind) good = row[x] == i * 2 + 1;
                else { float f; memcpy(&f, &row[x], 4); good = f == (float)i * 2.0f + 1.0f; }
                if (!good) { bad++; if (first == ~0u) first = i; }
            }
        const unsigned *row0 = (const unsigned *)m.pData;
        printf("%s: %u/%u texels wrong (first %d); texel 5 = 0x%08x\n", name, bad, W * H,
               (int)first, row0[5]);
        ok = bad == 0;
    }
    ctx->Unmap(staging, 0);
out:
    if (cs) cs->Release();
    if (code) code->Release();
    if (err) err->Release();
    if (uav) uav->Release();
    if (staging) staging->Release();
    if (tex) tex->Release();
    return ok;
}

int main()
{
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1,
                                   D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (FAILED(hr)) { printf("D3D11CreateDevice(HARDWARE, 11_0) failed: 0x%08lx\n", hr); return 2; }
    IDXGIDevice *dxgi = nullptr;
    IDXGIAdapter *adapter = nullptr;
    DXGI_ADAPTER_DESC ad = {};
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi)) &&
        SUCCEEDED(dxgi->GetAdapter(&adapter)))
        adapter->GetDesc(&ad);
    printf("adapter: %ls\nfeature level: 0x%x\n", ad.Description, got);
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 o2 = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &o2, sizeof o2)))
        printf("TypedUAVLoadAdditionalFormats: %d\n", o2.TypedUAVLoadAdditionalFormats);
    const DXGI_FORMAT fmts[] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT,
                                DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT};
    const char *names[] = {"R32_FLOAT", "R32_UINT", "R32_SINT", "R8G8B8A8_UNORM", "R16G16B16A16_FLOAT"};
    for (int i = 0; i < 5; i++) {
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 f2 = {fmts[i], 0};
        dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &f2, sizeof f2);
        printf("UAV typed load %s: %d\n", names[i],
               (f2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) ? 1 : 0);
    }
    const bool a = run(dev, ctx, 0), b = run(dev, ctx, 1);
    printf("result: %s\n", a && b ? "pass" : "fail");
    return a && b ? 0 : 1;
}
