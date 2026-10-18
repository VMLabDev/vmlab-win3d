// d3d11_rtt <n> <out>: D3D11 ClearRenderTargetView + Flush, then spin on an
// EVENT query until done. Measures the UMD -> KMD -> host -> completion round trip.
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 200;
    FILE *out = argc > 2 ? fopen(argv[2], "w") : stdout;
    setvbuf(out, NULL, _IONBF, 0);
    ID3D11Device *dev; ID3D11DeviceContext *ctx; D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
    if (FAILED(hr)) { fprintf(out, "CreateDevice 0x%08lx\n", hr); return 1; }
    D3D11_TEXTURE2D_DESC td = {64, 64, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    ID3D11Texture2D *tex; dev->CreateTexture2D(&td, NULL, &tex);
    ID3D11RenderTargetView *rtv; dev->CreateRenderTargetView(tex, NULL, &rtv);
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    ID3D11Query *q; dev->CreateQuery(&qd, &q);
    LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f);
    std::vector<double> v; long long polls = 0;
    for (int i = 0; i < n; i++) {
        float c[4] = {i / (float)n, 0, 0, 1};
        QueryPerformanceCounter(&a);
        ctx->ClearRenderTargetView(rtv, c);
        ctx->End(q);
        ctx->Flush();
        BOOL done = FALSE;
        while (ctx->GetData(q, &done, sizeof done, 0) != S_OK) polls++;
        QueryPerformanceCounter(&b);
        v.push_back(1000.0 * (b.QuadPart - a.QuadPart) / f.QuadPart);
        Sleep(2);
    }
    std::sort(v.begin(), v.end());
    double s = 0; for (double x : v) s += x;
    fprintf(out, "d3d11 event round trip: n=%d mean=%.3f p50=%.3f p95=%.3f max=%.3f min=%.3f polls=%lld\n", n, s / n, v[n / 2], v[n * 95 / 100], v[n - 1], v[0], polls);
    return 0;
}
