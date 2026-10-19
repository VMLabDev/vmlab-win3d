/* ExecuteIndirect repro: command signatures that change root state.
 *
 * Renders the same work on the default D3D12 device (vkd3d-proton on Venus
 * after the package is installed) and on WARP, reads both back and compares
 * them. WARP runs in its own process, from a directory holding Microsoft's
 * d3d12.dll (System32\vmlab-win3d\d3d12-native.dll, copied as d3d12.dll,
 * with System32\D3D12Core.dll beside it), since the stock runtime does not
 * load under another name. run-repro.sh sets this up.
 *
 * graphics: signature {root constants (colour), root CBV (cell offset), draw}.
 *   16 sequences, one quad per cell of a 4x4 grid, count buffer = 13, so
 *   three cells stay clear. A root constant outside the signature (quad
 *   size) is set on the CPU and must reach every sequence.
 * compute: signature {root constant (base index), dispatch}, 8 sequences of
 *   4 groups, each group writing base * 7 + group + 1 to a root UAV at
 *   base + group.
 *
 * usage: ei_repro.exe gpu <dir>       default adapter -> <dir>\ei_gpu.bin, .ppm
 *        ei_repro.exe warp <dir>      WARP adapter    -> <dir>\ei_warp.bin, .ppm
 *        ei_repro.exe compare <dir>   both against the expected values and each other
 * Exit 0 when the checks pass, 1 otherwise. */
#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 128, H = 128, CELLS = 16, DRAWN = 13, CS_SEQ = 8, CS_GROUPS = 4, UAV_WORDS = 64 };

typedef HRESULT(WINAPI *compile_fn)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
                                    ID3DInclude *, const char *, const char *, UINT, UINT, ID3DBlob **,
                                    ID3DBlob **);

static compile_fn compile;

static const char gfx_hlsl[] =
    "cbuffer Colour : register(b0) { float4 colour; };\n"
    "cbuffer Cell : register(b1) { float4 cell; };\n"
    "cbuffer Size : register(b2) { float size; };\n"
    "float4 vs(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 c[6] = { float2(0,0), float2(1,0), float2(0,1), float2(1,0), float2(1,1), float2(0,1) };\n"
    "  float2 p = cell.xy + c[id] * size;\n"
    "  return float4(p, 0.0, 1.0);\n"
    "}\n"
    "float4 ps() : SV_Target { return colour; }\n";

static const char cs_hlsl[] =
    "cbuffer Base : register(b0) { uint base; };\n"
    "RWByteAddressBuffer o : register(u0);\n"
    "[numthreads(1,1,1)] void cs(uint3 g : SV_GroupID) { o.Store((base + g.x) * 4, base * 7 + g.x + 1); }\n";

#define CHECK(expr, what)                                                       \
    do {                                                                        \
        HRESULT hr_ = (expr);                                                   \
        if (FAILED(hr_)) {                                                      \
            printf("FAIL %s: %s hr=0x%08lx\n", tag, what, (unsigned long)hr_); \
            return false;                                                       \
        }                                                                       \
    } while (0)

static ID3DBlob *compile_hlsl(const char *src, const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    if (FAILED(compile(src, strlen(src), "ei", NULL, NULL, entry, target, 0, 0, &code, &err))) {
        printf("FAIL compile %s: %s\n", entry, err ? (const char *)err->GetBufferPointer() : "?");
        return NULL;
    }
    return code;
}

static ID3D12Resource *make_buffer(ID3D12Device *dev, D3D12_HEAP_TYPE heap, UINT64 size,
                                   D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = heap;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    ID3D12Resource *r = NULL;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL,
                                            __uuidof(ID3D12Resource), (void **)&r)))
        return NULL;
    return r;
}

static D3D12_RESOURCE_BARRIER transition(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

static void *map_all(ID3D12Resource *r)
{
    void *p = NULL;
    r->Map(0, NULL, &p);
    return p;
}

struct result {
    unsigned int pixels[W * H];
    unsigned int uav[UAV_WORDS];
};

static bool root_signature(const char *tag, ID3D12Device *dev, const D3D12_ROOT_SIGNATURE_DESC *rsd,
                           ID3D12RootSignature **out)
{
    ID3DBlob *blob = NULL, *err = NULL;
    CHECK(D3D12SerializeRootSignature(rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "D3D12SerializeRootSignature");
    CHECK(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature),
                                   (void **)out),
          "CreateRootSignature");
    blob->Release();
    return true;
}

static bool run(const char *tag, ID3D12Device *dev, struct result *res)
{
    /* Graphics root signature: b0 constants (colour), b1 root CBV (cell), b2 constant (size). */
    D3D12_ROOT_PARAMETER gp[3] = {};
    gp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    gp[0].Constants.ShaderRegister = 0;
    gp[0].Constants.Num32BitValues = 4;
    gp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    gp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    gp[1].Descriptor.ShaderRegister = 1;
    gp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    gp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    gp[2].Constants.ShaderRegister = 2;
    gp[2].Constants.Num32BitValues = 1;
    gp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC grsd = {3, gp, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3D12RootSignature *grs = NULL;
    if (!root_signature(tag, dev, &grsd, &grs))
        return false;

    /* Compute root signature: b0 constant (base), u0 root UAV. */
    D3D12_ROOT_PARAMETER cp[2] = {};
    cp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    cp[0].Constants.Num32BitValues = 1;
    cp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    cp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    cp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC crsd = {2, cp, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3D12RootSignature *crs = NULL;
    if (!root_signature(tag, dev, &crsd, &crs))
        return false;

    ID3DBlob *vs = compile_hlsl(gfx_hlsl, "vs", "vs_5_0");
    ID3DBlob *ps = compile_hlsl(gfx_hlsl, "ps", "ps_5_0");
    ID3DBlob *cs = compile_hlsl(cs_hlsl, "cs", "cs_5_0");
    if (!vs || !ps || !cs)
        return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = grs;
    pd.VS.pShaderBytecode = vs->GetBufferPointer();
    pd.VS.BytecodeLength = vs->GetBufferSize();
    pd.PS.pShaderBytecode = ps->GetBufferPointer();
    pd.PS.BytecodeLength = ps->GetBufferSize();
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pd.SampleMask = 0xffffffffu;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    ID3D12PipelineState *gpso = NULL, *cpso = NULL;
    CHECK(dev->CreateGraphicsPipelineState(&pd, __uuidof(ID3D12PipelineState), (void **)&gpso),
          "CreateGraphicsPipelineState");
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {};
    cd.pRootSignature = crs;
    cd.CS.pShaderBytecode = cs->GetBufferPointer();
    cd.CS.BytecodeLength = cs->GetBufferSize();
    CHECK(dev->CreateComputePipelineState(&cd, __uuidof(ID3D12PipelineState), (void **)&cpso),
          "CreateComputePipelineState");

    /* Command signatures. */
    D3D12_INDIRECT_ARGUMENT_DESC ga[3] = {};
    ga[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    ga[0].Constant.RootParameterIndex = 0;
    ga[0].Constant.DestOffsetIn32BitValues = 0;
    ga[0].Constant.Num32BitValuesToSet = 4;
    ga[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    ga[1].ConstantBufferView.RootParameterIndex = 1;
    ga[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    /* Record: colour (16 bytes), CBV address (8), draw arguments (16), padding (8). */
    const UINT gstride = 48;
    D3D12_COMMAND_SIGNATURE_DESC gsd = {gstride, 3, ga, 0};
    ID3D12CommandSignature *gsig = NULL;
    CHECK(dev->CreateCommandSignature(&gsd, grs, __uuidof(ID3D12CommandSignature), (void **)&gsig),
          "CreateCommandSignature(graphics)");

    D3D12_INDIRECT_ARGUMENT_DESC ca[2] = {};
    ca[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    ca[0].Constant.RootParameterIndex = 0;
    ca[0].Constant.Num32BitValuesToSet = 1;
    ca[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    const UINT cstride = 16;
    D3D12_COMMAND_SIGNATURE_DESC csd = {cstride, 2, ca, 0};
    ID3D12CommandSignature *csig = NULL;
    CHECK(dev->CreateCommandSignature(&csd, crs, __uuidof(ID3D12CommandSignature), (void **)&csig),
          "CreateCommandSignature(compute)");

    /* Per-cell constant buffers, 256 bytes apart. */
    ID3D12Resource *cb = make_buffer(dev, D3D12_HEAP_TYPE_UPLOAD, 256 * CELLS, D3D12_RESOURCE_STATE_GENERIC_READ,
                                     D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource *args = make_buffer(dev, D3D12_HEAP_TYPE_UPLOAD, 4096, D3D12_RESOURCE_STATE_GENERIC_READ,
                                       D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource *uav = make_buffer(dev, D3D12_HEAP_TYPE_DEFAULT, UAV_WORDS * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *rb_uav = make_buffer(dev, D3D12_HEAP_TYPE_READBACK, UAV_WORDS * 4, D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_FLAG_NONE);
    if (!cb || !args || !uav || !rb_uav) {
        printf("FAIL %s: buffers\n", tag);
        return false;
    }
    unsigned char *cbp = (unsigned char *)map_all(cb);
    unsigned char *ap = (unsigned char *)map_all(args);
    memset(ap, 0, 4096);
    for (int i = 0; i < CELLS; i++) {
        float *cell = (float *)(cbp + 256 * i);
        cell[0] = -1.0f + 0.5f * (i % 4) + 0.05f;
        cell[1] = -1.0f + 0.5f * (i / 4) + 0.05f;
        cell[2] = cell[3] = 0.0f;
        unsigned char *rec = ap + gstride * i;
        float colour[4] = {(i & 1) ? 1.0f : 0.25f, (i & 2) ? 1.0f : 0.5f, (i & 4) ? 1.0f : 0.0f,
                           (i & 8) ? 1.0f : 0.75f};
        memcpy(rec, colour, 16);
        D3D12_GPU_VIRTUAL_ADDRESS va = cb->GetGPUVirtualAddress() + 256 * i;
        memcpy(rec + 16, &va, 8);
        D3D12_DRAW_ARGUMENTS draw = {6, 1, 0, 0};
        memcpy(rec + 24, &draw, sizeof draw);
    }
    const UINT count_offset = 2048, cs_offset = 1024;
    unsigned int drawn = DRAWN;
    memcpy(ap + count_offset, &drawn, 4);
    for (int i = 0; i < CS_SEQ; i++) {
        unsigned int rec[4] = {(unsigned int)i * CS_GROUPS, CS_GROUPS, 1, 1};
        memcpy(ap + cs_offset + cstride * i, rec, 16);
    }

    /* Render target and readback. */
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = W;
    td.Height = H;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear = {DXGI_FORMAT_R8G8B8A8_UNORM, {{0.0f, 0.0f, 0.0f, 1.0f}}};
    ID3D12Resource *rt = NULL;
    CHECK(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                       __uuidof(ID3D12Resource), (void **)&rt),
          "CreateCommittedResource(rt)");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT64 rb_bytes = 0;
    dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, NULL, NULL, &rb_bytes);
    ID3D12Resource *rb = make_buffer(dev, D3D12_HEAP_TYPE_READBACK, rb_bytes, D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_FLAG_NONE);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ID3D12DescriptorHeap *rtv_heap = NULL;
    CHECK(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap), "CreateDescriptorHeap");
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    dev->CreateRenderTargetView(rt, NULL, rtv);

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = NULL;
    ID3D12CommandAllocator *alloc = NULL;
    ID3D12GraphicsCommandList *cl = NULL;
    CHECK(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue), "CreateCommandQueue");
    CHECK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                      (void **)&alloc),
          "CreateCommandAllocator");
    CHECK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, gpso, __uuidof(ID3D12GraphicsCommandList),
                                 (void **)&cl),
          "CreateCommandList");

    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    cl->ClearRenderTargetView(rtv, black, 0, NULL);
    cl->OMSetRenderTargets(1, &rtv, FALSE, NULL);
    D3D12_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
    D3D12_RECT sc = {0, 0, W, H};
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->SetGraphicsRootSignature(grs);
    float size = 0.4f;
    cl->SetGraphicsRoot32BitConstants(2, 1, &size, 0);
    /* Garbage in the signature's own parameters: ExecuteIndirect must replace it. */
    const float junk[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    cl->SetGraphicsRoot32BitConstants(0, 4, junk, 0);
    cl->SetGraphicsRootConstantBufferView(1, cb->GetGPUVirtualAddress());
    cl->ExecuteIndirect(gsig, CELLS, args, 0, args, count_offset);

    cl->SetPipelineState(cpso);
    cl->SetComputeRootSignature(crs);
    cl->SetComputeRootUnorderedAccessView(1, uav->GetGPUVirtualAddress());
    cl->ExecuteIndirect(csig, CS_SEQ, args, cs_offset, NULL, 0);

    D3D12_RESOURCE_BARRIER b[2] = {
        transition(rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
        transition(uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
    };
    cl->ResourceBarrier(2, b);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = rb;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    src.pResource = rt;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
    cl->CopyBufferRegion(rb_uav, 0, uav, 0, UAV_WORDS * 4);
    CHECK(cl->Close(), "Close");

    ID3D12CommandList *lists[] = {cl};
    queue->ExecuteCommandLists(1, lists);
    ID3D12Fence *fence = NULL;
    CHECK(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence), "CreateFence");
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    queue->Signal(fence, 1);
    fence->SetEventOnCompletion(1, ev);
    if (WaitForSingleObject(ev, 20000) != WAIT_OBJECT_0) {
        printf("FAIL %s: GPU timeout\n", tag);
        return false;
    }
    CHECK(dev->GetDeviceRemovedReason(), "device removed");

    unsigned char *p = (unsigned char *)map_all(rb);
    for (int y = 0; y < H; y++)
        memcpy(res->pixels + y * W, p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, W * 4);
    memcpy(res->uav, map_all(rb_uav), sizeof res->uav);
    return true;
}

static void write_ppm(const char *dir, const char *name, const unsigned int *px)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char rgb[3] = {(unsigned char)(px[i] & 0xff), (unsigned char)((px[i] >> 8) & 0xff),
                                (unsigned char)((px[i] >> 16) & 0xff)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static unsigned int expected_cell_colour(int i)
{
    unsigned int r = (i & 1) ? 255 : 64, g = (i & 2) ? 255 : 128, b = (i & 4) ? 255 : 0, a = (i & 8) ? 255 : 191;
    return r | g << 8 | b << 16 | a << 24;
}

/* Rows of the image run top to bottom; NDC y runs bottom to top. */
static unsigned int cell_centre(const unsigned int *px, int i)
{
    int x = (i % 4) * 32 + 16, y = H - 1 - ((i / 4) * 32 + 16);
    return px[y * W + x];
}

/* UNORM conversion of 0.5 may round either way (127 or 128): allow 1/255 per channel. */
static bool close_enough(unsigned int a, unsigned int b)
{
    for (int c = 0; c < 32; c += 8) {
        int d = (int)((a >> c) & 0xff) - (int)((b >> c) & 0xff);
        if (d > 1 || d < -1)
            return false;
    }
    return true;
}

static int check(const char *tag, const struct result *r)
{
    int bad = 0;
    for (int i = 0; i < CELLS; i++) {
        unsigned int want = i < DRAWN ? expected_cell_colour(i) : 0xff000000u;
        unsigned int got = cell_centre(r->pixels, i);
        if (!close_enough(got, want)) {
            printf("FAIL %s: cell %d centre 0x%08x, want 0x%08x\n", tag, i, got, want);
            bad++;
        }
    }
    for (int i = 0; i < UAV_WORDS; i++) {
        unsigned int want = i < CS_SEQ * CS_GROUPS ? (unsigned int)((i / CS_GROUPS) * CS_GROUPS * 7 + i % CS_GROUPS + 1) : 0;
        if (r->uav[i] != want) {
            printf("FAIL %s: uav[%d] = %u, want %u\n", tag, i, r->uav[i], want);
            bad++;
        }
    }
    printf("%s %s: %d cell centres, %d UAV words checked\n", bad ? "FAIL" : "PASS", tag, CELLS, UAV_WORDS);
    return bad;
}

static bool save(const char *path, const struct result *r)
{
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(r, sizeof *r, 1, f) == 1;
    if (f)
        fclose(f);
    return ok;
}

static bool load(const char *path, struct result *r)
{
    FILE *f = fopen(path, "rb");
    bool ok = f && fread(r, sizeof *r, 1, f) == 1;
    if (f)
        fclose(f);
    return ok;
}

int main(int argc, char **argv)
{
    static struct result gpu, warp;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 3 || (strcmp(argv[1], "gpu") && strcmp(argv[1], "warp") && strcmp(argv[1], "compare"))) {
        printf("usage: ei_repro gpu|warp|compare <directory>\n");
        return 2;
    }
    const char *mode = argv[1], *dir = argv[2];
    char gpu_path[MAX_PATH], warp_path[MAX_PATH];
    snprintf(gpu_path, sizeof gpu_path, "%s\\ei_gpu.bin", dir);
    snprintf(warp_path, sizeof warp_path, "%s\\ei_warp.bin", dir);

    if (!strcmp(mode, "compare")) {
        if (!load(gpu_path, &gpu) || !load(warp_path, &warp)) {
            printf("FAIL: cannot read %s and %s\n", gpu_path, warp_path);
            return 1;
        }
        int bad = check("gpu", &gpu) + check("warp", &warp);
        int diff = 0, lit = 0;
        for (int i = 0; i < W * H; i++) {
            unsigned int a = gpu.pixels[i], b = warp.pixels[i];
            lit += a != 0xff000000u;
            diff += !close_enough(a, b);
        }
        printf("%s gpu-vs-warp: %d of %d pixels differ by more than 1/255 (gpu has %d non-clear pixels)\n",
               diff ? "FAIL" : "PASS", diff, W * H, lit);
        bad += diff != 0;
        if (memcmp(gpu.uav, warp.uav, sizeof gpu.uav)) {
            printf("FAIL gpu-vs-warp: UAV contents differ\n");
            bad++;
        } else {
            printf("PASS gpu-vs-warp: UAV contents identical\n");
        }
        printf("%s\n", bad ? "RESULT FAIL" : "RESULT PASS");
        return bad ? 1 : 0;
    }

    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!dc || !(compile = (compile_fn)(void *)GetProcAddress(dc, "D3DCompile"))) {
        printf("FAIL: d3dcompiler_47.dll\n");
        return 1;
    }

    /* "warp" runs from a directory holding Microsoft's d3d12.dll (the package keeps it as
     * System32\vmlab-win3d\d3d12-native.dll), so WARP gets the stock runtime. */
    bool use_warp = !strcmp(mode, "warp");
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory4 *factory = NULL;
    if (use_warp && (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void **)&factory)) ||
                     FAILED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void **)&adapter)))) {
        printf("FAIL: EnumWarpAdapter\n");
        return 1;
    }
    wchar_t module[MAX_PATH] = L"?";
    GetModuleFileNameW(GetModuleHandleW(L"d3d12.dll"), module, MAX_PATH);
    printf("%s: d3d12.dll is %ls\n", mode, module);
    ID3D12Device *dev = NULL;
    HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&dev);
    if (FAILED(hr)) {
        printf("FAIL: D3D12CreateDevice(%s) hr=0x%08lx\n", mode, (unsigned long)hr);
        return 1;
    }
    struct result *r = use_warp ? &warp : &gpu;
    if (!run(mode, dev, r))
        return 1;
    write_ppm(dir, use_warp ? "ei_warp.ppm" : "ei_gpu.ppm", r->pixels);
    int bad = check(mode, r);
    if (!save(use_warp ? warp_path : gpu_path, r)) {
        printf("FAIL: cannot write results\n");
        return 1;
    }
    return bad ? 1 : 0;
}
