/* Direct3D 12 probe: D3D12CreateDevice on the default adapter (NULL adapter),
 * or the WARP adapter with --warp, a flip-model swap chain, vs_5_0/ps_5_0
 * shaders, readback through a copy into a readback-heap buffer. */
/* Methods that return a struct (GetCPUDescriptorHandleForHeapStart, GetDesc,
 * GetAdapterLuid) use a hidden return pointer in the MSVC COM ABI; this makes
 * the mingw headers call them that way. */
#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include "../common/probe.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/dxgi_util.h"

static const char hlsl[] =
    "struct VI { float2 pos : POSITION; float4 col : COLOR; };\n"
    "struct VO { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "VO vs(VI i) { VO o; o.pos = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }\n"
    "float4 ps(VO i) : SV_Target { return i.col; }\n";

enum { BUFFERS = 2 };

static const char *fl_name(D3D_FEATURE_LEVEL fl)
{
    switch (fl) {
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    default: return "other";
    }
}

static D3D12_RESOURCE_BARRIER transition(ID3D12Resource *r, D3D12_RESOURCE_STATES from,
                                         D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

static ID3D12Resource *buffer(probe_ctx *ctx, ID3D12Device *dev, D3D12_HEAP_TYPE heap, UINT64 size,
                              D3D12_RESOURCE_STATES state, const char *what)
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
    ID3D12Resource *r = NULL;
    if (probe_hr(ctx, dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL,
                                                   __uuidof(ID3D12Resource), (void **)&r),
                 what))
        return NULL;
    return r;
}

static int run(probe_ctx *ctx)
{
    IDXGIFactory4 *factory = NULL;
    if (probe_hr(ctx, CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void **)&factory),
                 "CreateDXGIFactory1(IDXGIFactory4)"))
        return 1;

    IDXGIAdapter *warp = NULL;
    if (ctx->warp &&
        probe_hr(ctx, factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void **)&warp),
                 "IDXGIFactory4::EnumWarpAdapter"))
        return 1;
    probe_str(ctx, PROBE_API, "adapter_selection", ctx->warp ? "EnumWarpAdapter" : "default (NULL)");

    ID3D12Device *dev = NULL;
    if (probe_hr(ctx, D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                        (void **)&dev),
                 ctx->warp ? "D3D12CreateDevice(WARP adapter)" : "D3D12CreateDevice(default adapter)"))
        return 1;

    LUID luid = dev->GetAdapterLuid();
    IDXGIAdapter *adapter = NULL;
    if (!probe_hr(ctx, factory->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter), (void **)&adapter),
                  "IDXGIFactory4::EnumAdapterByLuid")) {
        describe_adapter(ctx, adapter, ctx->warp);
        adapter->Release();
    }

    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
                                               D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                               D3D_FEATURE_LEVEL_11_0};
    D3D12_FEATURE_DATA_FEATURE_LEVELS fls = {};
    fls.NumFeatureLevels = ARRAYSIZE(levels);
    fls.pFeatureLevelsRequested = levels;
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fls, sizeof fls)))
        probe_str(ctx, PROBE_API, "max_feature_level", fl_name(fls.MaxSupportedFeatureLevel));
    D3D12_FEATURE_DATA_SHADER_MODEL sm = {D3D_SHADER_MODEL_6_6};
    while (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) &&
           sm.HighestShaderModel > D3D_SHADER_MODEL_5_1)
        sm.HighestShaderModel = (D3D_SHADER_MODEL)(sm.HighestShaderModel - 1);
    char smv[16];
    snprintf(smv, sizeof smv, "%d.%d", sm.HighestShaderModel >> 4, sm.HighestShaderModel & 15);
    probe_str(ctx, PROBE_API, "highest_shader_model", smv);

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = NULL;
    if (probe_hr(ctx, dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue),
                 "CreateCommandQueue"))
        return 1;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = (UINT)ctx->width;
    sd.Height = (UINT)ctx->height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = BUFFERS;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *swap1 = NULL;
    IDXGISwapChain3 *swap = NULL;
    if (probe_hr(ctx, factory->CreateSwapChainForHwnd(queue, ctx->hwnd, &sd, NULL, NULL, &swap1),
                 "IDXGIFactory2::CreateSwapChainForHwnd") ||
        probe_hr(ctx, swap1->QueryInterface(__uuidof(IDXGISwapChain3), (void **)&swap),
                 "QueryInterface(IDXGISwapChain3)"))
        return 1;
    swap1->Release();
    factory->MakeWindowAssociation(ctx->hwnd, DXGI_MWA_NO_ALT_ENTER);
    probe_str(ctx, PROBE_API, "swap_effect", "FLIP_DISCARD");

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = BUFFERS;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    if (probe_hr(ctx, dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap),
                 "CreateDescriptorHeap(RTV)"))
        return 1;
    UINT rtv_step = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv0 = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    ID3D12Resource *bufs[BUFFERS] = {};
    for (UINT i = 0; i < BUFFERS; i++) {
        if (probe_hr(ctx, swap->GetBuffer(i, __uuidof(ID3D12Resource), (void **)&bufs[i]), "GetBuffer"))
            return 1;
        D3D12_CPU_DESCRIPTOR_HANDLE h = {rtv0.ptr + i * rtv_step};
        dev->CreateRenderTargetView(bufs[i], NULL, h);
    }

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob *rs_blob = NULL, *rs_err = NULL;
    if (probe_hr(ctx, D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err),
                 "D3D12SerializeRootSignature"))
        return 1;
    ID3D12RootSignature *root = NULL;
    if (probe_hr(ctx, dev->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                                               __uuidof(ID3D12RootSignature), (void **)&root),
                 "CreateRootSignature"))
        return 1;
    rs_blob->Release();

    size_t vs_size = 0, ps_size = 0;
    void *vs_code = probe_compile_hlsl(ctx, hlsl, "vs", "vs_5_0", &vs_size);
    void *ps_code = probe_compile_hlsl(ctx, hlsl, "ps", "ps_5_0", &ps_size);
    if (!vs_code || !ps_code)
        return 1;
    static const D3D12_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = root;
    pd.VS.pShaderBytecode = vs_code;
    pd.VS.BytecodeLength = vs_size;
    pd.PS.pShaderBytecode = ps_code;
    pd.PS.BytecodeLength = ps_size;
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
    pd.InputLayout.pInputElementDescs = elems;
    pd.InputLayout.NumElements = 2;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    ID3D12PipelineState *pso = NULL;
    if (probe_hr(ctx, dev->CreateGraphicsPipelineState(&pd, __uuidof(ID3D12PipelineState), (void **)&pso),
                 "CreateGraphicsPipelineState"))
        return 1;
    free(vs_code);
    free(ps_code);

    ID3D12CommandAllocator *alloc = NULL;
    ID3D12GraphicsCommandList *cl = NULL;
    if (probe_hr(ctx, dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  __uuidof(ID3D12CommandAllocator), (void **)&alloc),
                 "CreateCommandAllocator") ||
        probe_hr(ctx, dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, pso,
                                             __uuidof(ID3D12GraphicsCommandList), (void **)&cl),
                 "CreateCommandList"))
        return 1;
    cl->Close();

    const UINT vb_bytes = PROBE_MAX_VERTICES * sizeof(probe_vertex);
    ID3D12Resource *vb = buffer(ctx, dev, D3D12_HEAP_TYPE_UPLOAD, vb_bytes,
                                D3D12_RESOURCE_STATE_GENERIC_READ, "CreateCommittedResource(vertex)");
    if (!vb)
        return 1;
    probe_vertex *verts = NULL;
    D3D12_RANGE none = {0, 0};
    if (probe_hr(ctx, vb->Map(0, &none, (void **)&verts), "Map(vertex)"))
        return 1;

    D3D12_RESOURCE_DESC bbd = bufs[0]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT64 rb_bytes = 0;
    dev->GetCopyableFootprints(&bbd, 0, 1, 0, &fp, NULL, NULL, &rb_bytes);
    ID3D12Resource *rb = buffer(ctx, dev, D3D12_HEAP_TYPE_READBACK, rb_bytes,
                                D3D12_RESOURCE_STATE_COPY_DEST, "CreateCommittedResource(readback)");
    if (!rb)
        return 1;

    ID3D12Fence *fence = NULL;
    UINT64 fence_value = 0;
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (probe_hr(ctx, dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence),
                 "CreateFence"))
        return 1;

    D3D12_VIEWPORT vp = {0, 0, (float)ctx->width, (float)ctx->height, 0, 1};
    D3D12_RECT scissor = {0, 0, ctx->width, ctx->height};
    D3D12_VERTEX_BUFFER_VIEW vbv = {vb->GetGPUVirtualAddress(), vb_bytes, sizeof(probe_vertex)};

    for (;;) {
        probe_pump(ctx);
        int last = probe_last_frame(ctx);
        /* One frame in flight: the previous frame's fence has been waited on. */
        int n = probe_build_scene(ctx, probe_now(ctx), verts, PROBE_MAX_VERTICES);
        UINT idx = swap->GetCurrentBackBufferIndex();
        ID3D12Resource *bb = bufs[idx];
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = {rtv0.ptr + idx * rtv_step};

        if (probe_hr(ctx, alloc->Reset(), "ID3D12CommandAllocator::Reset") ||
            probe_hr(ctx, cl->Reset(alloc, pso), "ID3D12GraphicsCommandList::Reset"))
            return 1;
        D3D12_RESOURCE_BARRIER b =
            transition(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cl->ResourceBarrier(1, &b);
        cl->OMSetRenderTargets(1, &rtv, FALSE, NULL);
        cl->ClearRenderTargetView(rtv, probe_clear_color, 0, NULL);
        cl->SetGraphicsRootSignature(root);
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &scissor);
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->IASetVertexBuffers(0, 1, &vbv);
        cl->DrawInstanced((UINT)n, 1, 0, 0);
        if (last) {
            b = transition(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cl->ResourceBarrier(1, &b);
            D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
            dst.pResource = rb;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = fp;
            src.pResource = bb;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
            b = transition(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        } else {
            b = transition(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        }
        cl->ResourceBarrier(1, &b);
        if (probe_hr(ctx, cl->Close(), "ID3D12GraphicsCommandList::Close"))
            return 1;
        ID3D12CommandList *lists[] = {cl};
        queue->ExecuteCommandLists(1, lists);

        HRESULT hr = swap->Present(1, 0);
        if (hr == DXGI_STATUS_OCCLUDED) {
            /* The window is hidden; not a failure. */
        } else if (probe_hr(ctx, hr, "IDXGISwapChain::Present")) {
            return 1;
        } else {
            probe_frame_presented(ctx);
        }

        fence_value++;
        if (probe_hr(ctx, queue->Signal(fence, fence_value), "ID3D12CommandQueue::Signal"))
            return 1;
        if (fence->GetCompletedValue() < fence_value) {
            fence->SetEventOnCompletion(fence_value, event);
            if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) {
                probe_error(ctx, "GPU fence %llu not reached within 10 s (device removed reason 0x%08lx)",
                            (unsigned long long)fence_value, (unsigned long)dev->GetDeviceRemovedReason());
                return 1;
            }
        }
        if (last) {
            uint8_t *p = NULL;
            D3D12_RANGE all = {0, (SIZE_T)rb_bytes};
            if (probe_hr(ctx, rb->Map(0, &all, (void **)&p), "Map(readback)"))
                return 1;
            probe_readback_image(ctx, p + fp.Offset, fp.Footprint.RowPitch, 0, 0);
            rb->Unmap(0, &none);
            break;
        }
    }
    HRESULT removed = dev->GetDeviceRemovedReason();
    if (removed != S_OK)
        probe_error(ctx, "device removed: 0x%08lx", (unsigned long)removed);

    CloseHandle(event);
    fence->Release();
    rb->Release();
    vb->Unmap(0, NULL);
    vb->Release();
    cl->Release();
    alloc->Release();
    pso->Release();
    root->Release();
    for (UINT i = 0; i < BUFFERS; i++)
        bufs[i]->Release();
    rtv_heap->Release();
    swap->Release();
    queue->Release();
    dev->Release();
    if (warp)
        warp->Release();
    factory->Release();
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    probe_ctx ctx;
    int rc = probe_init(&ctx, argc, argv, "d3d12", "Direct3D 12", "DIRECT3D 12", PROBE_ALLOW_WARP);
    if (rc)
        return rc;
    if (ctx.hwnd)
        run(&ctx);
    return probe_finish(&ctx);
}
