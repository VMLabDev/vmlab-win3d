/* DXGI helpers shared by the Direct3D 11 and 12 probes (C++ only). */
#ifndef PROBE_DXGI_UTIL_H
#define PROBE_DXGI_UTIL_H

#include "probe.h"

#include <dxgi1_2.h>
#include <stdio.h>

/* Records the DXGI adapter description shared by the D3D11 and D3D12 probes. */
static inline void describe_adapter(probe_ctx *ctx, IDXGIAdapter *adapter, int is_warp)
{
    DXGI_ADAPTER_DESC1 d = {};
    IDXGIAdapter1 *a1 = NULL;
    if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter1), (void **)&a1))) {
        a1->GetDesc1(&d);
        a1->Release();
    } else {
        adapter->GetDesc((DXGI_ADAPTER_DESC *)&d);
    }
    const char *name = probe_utf8(d.Description);
    probe_str(ctx, PROBE_ADAPTER, "name", name);
    const char *vendor = probe_vendor_name(d.VendorId);
    probe_str(ctx, PROBE_ADAPTER, "vendor", vendor ? vendor : "");
    probe_hex(ctx, PROBE_ADAPTER, "vendor_id", d.VendorId);
    probe_hex(ctx, PROBE_ADAPTER, "device_id", d.DeviceId);
    probe_hex(ctx, PROBE_ADAPTER, "subsys_id", d.SubSysId);
    probe_int(ctx, PROBE_ADAPTER, "dedicated_video_memory_mb",
              (long long)(d.DedicatedVideoMemory >> 20));
    probe_int(ctx, PROBE_ADAPTER, "shared_system_memory_mb",
              (long long)(d.SharedSystemMemory >> 20));
    probe_bool(ctx, PROBE_ADAPTER, "dxgi_flag_software", (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0);
    LARGE_INTEGER umd;
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
        probe_str(ctx, PROBE_ADAPTER, "driver_version",
                  probe_version4((unsigned long long)umd.QuadPart));
    if (is_warp)
        probe_classify(ctx, name, (long)d.VendorId, 1, "--warp requested");
    else if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        probe_classify(ctx, name, (long)d.VendorId, 1, "DXGI_ADAPTER_FLAG_SOFTWARE");
    else
        probe_classify(ctx, name, (long)d.VendorId, 0, NULL);
    char sub[160];
    snprintf(sub, sizeof sub, "%s - %s", ctx->arch, name);
    probe_set_subtitle(ctx, sub);
}

#endif
