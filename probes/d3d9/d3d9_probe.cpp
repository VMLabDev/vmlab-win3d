/* Direct3D 9 probe: Direct3DCreate9Ex (or Direct3DCreate9), HAL device on the
 * default adapter, vs_2_0/ps_2_0 shaders, readback via GetRenderTargetData. */
#include "../common/probe.h"

#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

static const char hlsl[] =
    "struct VI { float2 pos : POSITION; float4 col : COLOR0; };\n"
    "struct VO { float4 pos : POSITION; float4 col : COLOR0; };\n"
    "VO vs(VI i) { VO o; o.pos = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }\n"
    "float4 ps(float4 col : COLOR0) : COLOR { return col; }\n";

static D3DCOLOR to_d3dcolor(const float c[4])
{
    return D3DCOLOR_COLORVALUE(c[0], c[1], c[2], c[3]);
}

static int run(probe_ctx *ctx)
{
    typedef HRESULT(WINAPI * create_ex_fn)(UINT, IDirect3D9Ex **);
    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    create_ex_fn create_ex =
        d3d9 ? (create_ex_fn)(void *)GetProcAddress(d3d9, "Direct3DCreate9Ex") : NULL;

    IDirect3D9Ex *d3dex = NULL;
    IDirect3D9 *d3d = NULL;
    if (create_ex && SUCCEEDED(create_ex(D3D_SDK_VERSION, &d3dex))) {
        d3d = d3dex;
        probe_str(ctx, PROBE_API, "create_function", "Direct3DCreate9Ex");
    } else {
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
        probe_str(ctx, PROBE_API, "create_function", "Direct3DCreate9");
    }
    if (!d3d) {
        probe_error(ctx, "Direct3DCreate9 returned NULL");
        return 1;
    }
    probe_int(ctx, PROBE_API, "adapter_count", d3d->GetAdapterCount());

    D3DADAPTER_IDENTIFIER9 id = {};
    if (!probe_hr(ctx, d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id),
                  "GetAdapterIdentifier")) {
        probe_str(ctx, PROBE_ADAPTER, "name", id.Description);
        const char *vendor = probe_vendor_name(id.VendorId);
        probe_str(ctx, PROBE_ADAPTER, "vendor", vendor ? vendor : "");
        probe_hex(ctx, PROBE_ADAPTER, "vendor_id", id.VendorId);
        probe_hex(ctx, PROBE_ADAPTER, "device_id", id.DeviceId);
        probe_str(ctx, PROBE_ADAPTER, "driver", id.Driver);
        probe_str(ctx, PROBE_ADAPTER, "driver_version",
                  probe_version4((unsigned long long)id.DriverVersion.QuadPart));
        probe_classify(ctx, id.Description, (long)id.VendorId, 0, NULL);
        char sub[160];
        snprintf(sub, sizeof sub, "%s - %s", ctx->arch, id.Description);
        probe_set_subtitle(ctx, sub);
    }

    D3DCAPS9 caps = {};
    DWORD vp = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    if (SUCCEEDED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps))) {
        char v[16];
        snprintf(v, sizeof v, "%lu.%lu", (unsigned long)D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion),
                 (unsigned long)D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion));
        probe_str(ctx, PROBE_API, "vertex_shader_version", v);
        snprintf(v, sizeof v, "%lu.%lu", (unsigned long)D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion),
                 (unsigned long)D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion));
        probe_str(ctx, PROBE_API, "pixel_shader_version", v);
        probe_bool(ctx, PROBE_API, "hw_transform_and_light",
                   (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0);
        if (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
            vp = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    }
    probe_str(ctx, PROBE_API, "vertex_processing",
              vp == D3DCREATE_HARDWARE_VERTEXPROCESSING ? "hardware" : "software");

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = (UINT)ctx->width;
    pp.BackBufferHeight = (UINT)ctx->height;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = ctx->hwnd;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    IDirect3DDevice9 *dev = NULL;
    HRESULT hr;
    if (d3dex) {
        IDirect3DDevice9Ex *devex = NULL;
        hr = d3dex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, ctx->hwnd,
                                   vp | D3DCREATE_FPU_PRESERVE, &pp, NULL, &devex);
        dev = devex;
    } else {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, ctx->hwnd,
                               vp | D3DCREATE_FPU_PRESERVE, &pp, &dev);
    }
    if (probe_hr(ctx, hr, "CreateDevice(D3DDEVTYPE_HAL)"))
        return 1;
    probe_str(ctx, PROBE_API, "device_type", "HAL");

    size_t vs_size = 0, ps_size = 0;
    void *vs_code = probe_compile_hlsl(ctx, hlsl, "vs", "vs_2_0", &vs_size);
    void *ps_code = probe_compile_hlsl(ctx, hlsl, "ps", "ps_2_0", &ps_size);
    if (!vs_code || !ps_code)
        return 1;
    IDirect3DVertexShader9 *vs = NULL;
    IDirect3DPixelShader9 *ps = NULL;
    if (probe_hr(ctx, dev->CreateVertexShader((const DWORD *)vs_code, &vs), "CreateVertexShader") ||
        probe_hr(ctx, dev->CreatePixelShader((const DWORD *)ps_code, &ps), "CreatePixelShader"))
        return 1;
    free(vs_code);
    free(ps_code);

    static const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 8, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END(),
    };
    IDirect3DVertexDeclaration9 *decl = NULL;
    if (probe_hr(ctx, dev->CreateVertexDeclaration(elements, &decl), "CreateVertexDeclaration"))
        return 1;

    IDirect3DVertexBuffer9 *vb = NULL;
    const UINT vb_bytes = PROBE_MAX_VERTICES * sizeof(probe_vertex);
    if (probe_hr(ctx,
                 dev->CreateVertexBuffer(vb_bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                         D3DPOOL_DEFAULT, &vb, NULL),
                 "CreateVertexBuffer"))
        return 1;

    IDirect3DSurface9 *sysmem = NULL;
    if (probe_hr(ctx,
                 dev->CreateOffscreenPlainSurface((UINT)ctx->width, (UINT)ctx->height,
                                                  D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysmem, NULL),
                 "CreateOffscreenPlainSurface"))
        return 1;

    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

    probe_vertex *verts = (probe_vertex *)malloc(vb_bytes);
    for (;;) {
        probe_pump(ctx);
        int last = probe_last_frame(ctx);
        int n = probe_build_scene(ctx, probe_now(ctx), verts, PROBE_MAX_VERTICES);

        void *dst = NULL;
        if (probe_hr(ctx, vb->Lock(0, (UINT)(n * sizeof(probe_vertex)), &dst, D3DLOCK_DISCARD),
                     "IDirect3DVertexBuffer9::Lock"))
            return 1;
        memcpy(dst, verts, n * sizeof(probe_vertex));
        vb->Unlock();

        if (probe_hr(ctx, dev->Clear(0, NULL, D3DCLEAR_TARGET, to_d3dcolor(probe_clear_color), 1.0f, 0),
                     "Clear") ||
            probe_hr(ctx, dev->BeginScene(), "BeginScene"))
            return 1;
        dev->SetVertexDeclaration(decl);
        dev->SetVertexShader(vs);
        dev->SetPixelShader(ps);
        dev->SetStreamSource(0, vb, 0, sizeof(probe_vertex));
        if (probe_hr(ctx, dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, (UINT)n / 3), "DrawPrimitive"))
            return 1;
        if (probe_hr(ctx, dev->EndScene(), "EndScene"))
            return 1;

        if (last) {
            IDirect3DSurface9 *bb = NULL;
            if (probe_hr(ctx, dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb), "GetBackBuffer") ||
                probe_hr(ctx, dev->GetRenderTargetData(bb, sysmem), "GetRenderTargetData"))
                return 1;
            bb->Release();
            D3DLOCKED_RECT lr;
            if (probe_hr(ctx, sysmem->LockRect(&lr, NULL, D3DLOCK_READONLY), "LockRect"))
                return 1;
            probe_readback_image(ctx, (const uint8_t *)lr.pBits, (size_t)lr.Pitch, 1, 0);
            sysmem->UnlockRect();
        }

        hr = dev->Present(NULL, NULL, NULL, NULL);
        if (hr == D3DERR_DEVICELOST || hr == S_PRESENT_OCCLUDED) {
            /* Not an API failure; keep going and let the frame count tell. */
        } else if (probe_hr(ctx, hr, "Present")) {
            return 1;
        }
        if (hr == D3D_OK)
            probe_frame_presented(ctx);
        if (last)
            break;
    }
    free(verts);
    sysmem->Release();
    vb->Release();
    decl->Release();
    ps->Release();
    vs->Release();
    dev->Release();
    d3d->Release();
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    probe_ctx ctx;
    int rc = probe_init(&ctx, argc, argv, "d3d9", "Direct3D 9", "DIRECT3D 9", 0);
    if (rc)
        return rc;
    if (ctx.hwnd)
        run(&ctx);
    return probe_finish(&ctx);
}
