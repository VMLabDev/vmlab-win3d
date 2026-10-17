/*
 * ddcaps: shows whether DirectDraw on the default display device runs on the
 * driver (HAL) or only on the software emulation layer (HEL), and proves the
 * HAL blits in video memory.
 *
 *   ddcaps.exe [--seconds N] [--out FILE]
 *
 * It prints one JSON object (and writes it to FILE):
 *   - the DirectDraw device identifier;
 *   - the HAL and HEL DDCAPS (dwCaps, dwCaps2, dwCKeyCaps, dwFXCaps, video
 *     memory), as hex and as flag names;
 *   - free and total local and non-local (AGP) video memory;
 *   - the Direct3D 7 devices DirectDraw offers (a HAL device needs a HAL);
 *   - a blit test on two 256x256 X8R8G8B8 off-screen surfaces created in
 *     DDSCAPS_VIDEOMEMORY: colour fill, copy blit, stretch blit, source
 *     colour-key blit and mirror blit, then a Lock and a check of the
 *     pixels. Each step says whether the HAL advertises it
 *     (DDCAPS_BLTSTRETCH, DDCKEYCAPS_SRCBLT, DDFXCAPS_BLTMIRRORLEFTRIGHT);
 *     DirectDraw emulates the others on the CPU through Lock;
 *   - with --seconds N > 0, a window that DirectDraw blits a moving pattern
 *     into through the primary surface and a clipper for N seconds, so the
 *     result shows on the console.
 * "pass" is true when the HAL exists (no DDCAPS_NOHARDWARE, DDCAPS_BLT set),
 * the surfaces are in video memory, and every blit check matches.
 * Exit code: 0 pass, 1 fail, 2 DirectDraw could not be created.
 */
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK ddraw.h values that mingw-w64's ddraw.h lacks. */
#ifndef DDCAPS2_RESERVED1
#define DDCAPS2_RESERVED1 0x08000000L
#endif
#ifndef DDCAPS2_CANMANAGERESOURCE
#define DDCAPS2_CANMANAGERESOURCE 0x10000000L
#endif
#ifndef DDCAPS2_DYNAMICTEXTURES
#define DDCAPS2_DYNAMICTEXTURES 0x20000000L
#endif
#ifndef DDCAPS2_CANAUTOGENMIPMAP
#define DDCAPS2_CANAUTOGENMIPMAP 0x40000000L
#endif

static char g_json[65536];
static size_t g_len;

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_json + g_len, sizeof(g_json) - g_len, fmt, ap);
    va_end(ap);
    if (n > 0 && g_len + (size_t)n < sizeof(g_json))
        g_len += (size_t)n;
}

static void out_str(const char *s)
{
    out("\"");
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            out("\\%c", c);
        else if (c < 0x20)
            out("\\u%04x", c);
        else
            out("%c", c);
    }
    out("\"");
}

struct flag { DWORD bit; const char *name; };
#define F(x) { x, #x }

static const struct flag caps_flags[] = {
    F(DDCAPS_3D), F(DDCAPS_ALIGNBOUNDARYDEST), F(DDCAPS_ALIGNSIZEDEST),
    F(DDCAPS_ALIGNBOUNDARYSRC), F(DDCAPS_ALIGNSIZESRC), F(DDCAPS_ALIGNSTRIDE),
    F(DDCAPS_BLT), F(DDCAPS_BLTQUEUE), F(DDCAPS_BLTFOURCC), F(DDCAPS_BLTSTRETCH),
    F(DDCAPS_GDI), F(DDCAPS_OVERLAY), F(DDCAPS_OVERLAYCANTCLIP),
    F(DDCAPS_OVERLAYFOURCC), F(DDCAPS_OVERLAYSTRETCH), F(DDCAPS_PALETTE),
    F(DDCAPS_PALETTEVSYNC), F(DDCAPS_READSCANLINE), F(DDCAPS_VBI),
    F(DDCAPS_ZBLTS), F(DDCAPS_ZOVERLAYS), F(DDCAPS_COLORKEY), F(DDCAPS_ALPHA),
    F(DDCAPS_COLORKEYHWASSIST), F(DDCAPS_NOHARDWARE), F(DDCAPS_BLTCOLORFILL),
    F(DDCAPS_BANKSWITCHED), F(DDCAPS_BLTDEPTHFILL), F(DDCAPS_CANCLIP),
    F(DDCAPS_CANCLIPSTRETCHED), F(DDCAPS_CANBLTSYSMEM),
};

static const struct flag caps2_flags[] = {
    F(DDCAPS2_CERTIFIED), F(DDCAPS2_NO2DDURING3DSCENE), F(DDCAPS2_VIDEOPORT),
    F(DDCAPS2_AUTOFLIPOVERLAY), F(DDCAPS2_CANBOBINTERLEAVED),
    F(DDCAPS2_CANBOBNONINTERLEAVED), F(DDCAPS2_COLORCONTROLOVERLAY),
    F(DDCAPS2_COLORCONTROLPRIMARY), F(DDCAPS2_CANDROPZ16BIT),
    F(DDCAPS2_NONLOCALVIDMEM), F(DDCAPS2_NONLOCALVIDMEMCAPS),
    F(DDCAPS2_NOPAGELOCKREQUIRED), F(DDCAPS2_WIDESURFACES),
    F(DDCAPS2_CANFLIPODDEVEN), F(DDCAPS2_CANBOBHARDWARE), F(DDCAPS2_COPYFOURCC),
    F(DDCAPS2_PRIMARYGAMMA), F(DDCAPS2_CANRENDERWINDOWED),
    F(DDCAPS2_CANCALIBRATEGAMMA), F(DDCAPS2_FLIPINTERVAL),
    F(DDCAPS2_FLIPNOVSYNC), F(DDCAPS2_CANMANAGETEXTURE),
    F(DDCAPS2_TEXMANINNONLOCALVIDMEM), F(DDCAPS2_STEREO),
    F(DDCAPS2_SYSTONONLOCAL_AS_SYSTOLOCAL), F(DDCAPS2_RESERVED1),
    F(DDCAPS2_CANMANAGERESOURCE), F(DDCAPS2_DYNAMICTEXTURES),
    F(DDCAPS2_CANAUTOGENMIPMAP),
};

static const struct flag ckey_flags[] = {
    F(DDCKEYCAPS_DESTBLT), F(DDCKEYCAPS_DESTBLTCLRSPACE),
    F(DDCKEYCAPS_DESTBLTCLRSPACEYUV), F(DDCKEYCAPS_DESTBLTYUV),
    F(DDCKEYCAPS_DESTOVERLAY), F(DDCKEYCAPS_SRCBLT),
    F(DDCKEYCAPS_SRCBLTCLRSPACE), F(DDCKEYCAPS_SRCBLTCLRSPACEYUV),
    F(DDCKEYCAPS_SRCBLTYUV), F(DDCKEYCAPS_SRCOVERLAY),
    F(DDCKEYCAPS_NOCOSTOVERLAY),
};

static const struct flag fx_flags[] = {
    F(DDFXCAPS_BLTARITHSTRETCHY), F(DDFXCAPS_BLTARITHSTRETCHYN),
    F(DDFXCAPS_BLTMIRRORLEFTRIGHT), F(DDFXCAPS_BLTMIRRORUPDOWN),
    F(DDFXCAPS_BLTROTATION), F(DDFXCAPS_BLTROTATION90),
    F(DDFXCAPS_BLTSHRINKX), F(DDFXCAPS_BLTSHRINKXN), F(DDFXCAPS_BLTSHRINKY),
    F(DDFXCAPS_BLTSHRINKYN), F(DDFXCAPS_BLTSTRETCHX), F(DDFXCAPS_BLTSTRETCHXN),
    F(DDFXCAPS_BLTSTRETCHY), F(DDFXCAPS_BLTSTRETCHYN), F(DDFXCAPS_BLTALPHA),
};

static const struct flag scaps_flags[] = {
    F(DDSCAPS_SYSTEMMEMORY), F(DDSCAPS_VIDEOMEMORY), F(DDSCAPS_LOCALVIDMEM),
    F(DDSCAPS_NONLOCALVIDMEM), F(DDSCAPS_OFFSCREENPLAIN), F(DDSCAPS_PRIMARYSURFACE),
    F(DDSCAPS_3DDEVICE), F(DDSCAPS_TEXTURE), F(DDSCAPS_HWCODEC),
};

static void out_flags(const char *key, DWORD v, const struct flag *f, size_t n)
{
    out("\"%s\": \"0x%08lx\", \"%s_names\": [", key, (unsigned long)v, key);
    int first = 1;
    for (size_t i = 0; i < n; i++) {
        if (v & f[i].bit) {
            out("%s\"%s\"", first ? "" : ", ", f[i].name);
            first = 0;
            v &= ~f[i].bit;
        }
    }
    if (v)
        out("%s\"0x%08lx\"", first ? "" : ", ", (unsigned long)v);
    out("]");
}

#define OUT_FLAGS(key, v, table) out_flags(key, v, table, sizeof(table) / sizeof(table[0]))

static void out_caps(const char *name, const DDCAPS *c)
{
    out("\"%s\": {", name);
    OUT_FLAGS("dwCaps", c->dwCaps, caps_flags); out(", ");
    OUT_FLAGS("dwCaps2", c->dwCaps2, caps2_flags); out(", ");
    OUT_FLAGS("dwCKeyCaps", c->dwCKeyCaps, ckey_flags); out(", ");
    OUT_FLAGS("dwFXCaps", c->dwFXCaps, fx_flags); out(", ");
    OUT_FLAGS("ddsCaps", c->ddsCaps.dwCaps, scaps_flags); out(", ");
    out("\"dwVidMemTotal\": %lu, \"dwVidMemFree\": %lu, \"dwSVBCaps\": \"0x%08lx\", "
        "\"dwVSBCaps\": \"0x%08lx\", \"dwSSBCaps\": \"0x%08lx\", \"dwNLVBCaps\": \"0x%08lx\"}",
        (unsigned long)c->dwVidMemTotal, (unsigned long)c->dwVidMemFree,
        (unsigned long)c->dwSVBCaps, (unsigned long)c->dwVSBCaps,
        (unsigned long)c->dwSSBCaps, (unsigned long)c->dwNLVBCaps);
}

static void out_vidmem(IDirectDraw7 *dd, const char *name, DWORD caps)
{
    DDSCAPS2 sc = {0};
    DWORD total = 0, freemem = 0;
    sc.dwCaps = caps;
    HRESULT hr = IDirectDraw7_GetAvailableVidMem(dd, &sc, &total, &freemem);
    out("\"%s\": {\"hr\": \"0x%08lx\", \"total\": %lu, \"free\": %lu}", name,
        (unsigned long)hr, (unsigned long)total, (unsigned long)freemem);
}

static int g_ndev;
static HRESULT CALLBACK enum_d3d7(LPSTR desc, LPSTR name, LPD3DDEVICEDESC7 d, LPVOID ctx)
{
    char guid[64];
    const GUID *g = &d->deviceGUID;
    snprintf(guid, sizeof(guid), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
             g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    out("%s{\"name\": ", g_ndev++ ? ", " : "");
    out_str(name);
    out(", \"description\": ");
    out_str(desc);
    out(", \"guid\": \"%s\", \"hal\": %s, \"dwDevCaps\": \"0x%08lx\"}", guid,
        (IsEqualGUID(g, &IID_IDirect3DHALDevice) || IsEqualGUID(g, &IID_IDirect3DTnLHalDevice))
            ? "true" : "false",
        (unsigned long)d->dwDevCaps);
    return D3DENUMRET_OK;
}

static IDirectDrawSurface7 *make_surface(IDirectDraw7 *dd, DWORD w, DWORD h, HRESULT *hr)
{
    DDSURFACEDESC2 sd = {0};
    IDirectDrawSurface7 *s = NULL;
    sd.dwSize = sizeof(sd);
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    sd.dwWidth = w;
    sd.dwHeight = h;
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    sd.ddpfPixelFormat.dwSize = sizeof(sd.ddpfPixelFormat);
    sd.ddpfPixelFormat.dwFlags = DDPF_RGB;
    sd.ddpfPixelFormat.dwRGBBitCount = 32;
    sd.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    sd.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    sd.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    *hr = IDirectDraw7_CreateSurface(dd, &sd, &s, NULL);
    return SUCCEEDED(*hr) ? s : NULL;
}

static HRESULT fill(IDirectDrawSurface7 *s, RECT *r, DWORD color)
{
    DDBLTFX fx = {0};
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = color;
    return IDirectDrawSurface7_Blt(s, r, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
}

#define RED 0x00ff0000u
#define GREEN 0x0000ff00u
#define BLUE 0x000000ffu
#define WHITE 0x00ffffffu
#define BLACK 0x00000000u

struct check { const char *what; int x, y; DWORD want; int step; };

/* Runs the blit sequence and checks the result. Returns 1 when every check
 * matched. dst ends up (256x256):
 *   copy of src (red with a blue 64x64 block at 0,0)          rows   0..63
 *   then a 2x stretch of src's top-left 32x32 into 64x64 at 0,64
 *   src colour-key blit (key blue) of src onto a green fill at 128..191
 *   mirror left-right of a white/black split at 192..255 */
static int blit_test(IDirectDraw7 *dd, const DDCAPS *hal)
{
    HRESULT hr;
    int ok = 1;
    out("\"blit\": {");
    IDirectDrawSurface7 *src = make_surface(dd, 256, 256, &hr);
    out("\"create_src_hr\": \"0x%08lx\"", (unsigned long)hr);
    IDirectDrawSurface7 *dst = make_surface(dd, 256, 256, &hr);
    out(", \"create_dst_hr\": \"0x%08lx\"", (unsigned long)hr);
    if (!src || !dst) {
        out(", \"pass\": false}");
        if (src) IDirectDrawSurface7_Release(src);
        if (dst) IDirectDrawSurface7_Release(dst);
        return 0;
    }
    DDSCAPS2 sc = {0};
    IDirectDrawSurface7_GetCaps(dst, &sc);
    out(", ");
    OUT_FLAGS("surface_caps", sc.dwCaps, scaps_flags);
    int in_vidmem = (sc.dwCaps & DDSCAPS_VIDEOMEMORY) != 0;
    out(", \"in_video_memory\": %s", in_vidmem ? "true" : "false");
    ok &= in_vidmem;

    RECT all = {0, 0, 256, 256};
    RECT blk = {0, 0, 64, 64};
    /* "hal" says whether the HAL advertises what a step needs. When it does
     * not, DirectDraw emulates the step on the CPU through Lock, which the
     * driver must also get right; every step counts either way. */
    struct { const char *name; HRESULT hr; int hal; } steps[8];
    const int stretch_hal = (hal->dwCaps & DDCAPS_BLTSTRETCH) != 0;
    const int ckey_hal = (hal->dwCKeyCaps & DDCKEYCAPS_SRCBLT) != 0;
    const int mirror_hal = (hal->dwFXCaps & DDFXCAPS_BLTMIRRORLEFTRIGHT) != 0;
    int n = 0;
    for (int i = 0; i < 8; i++)
        steps[i].hal = 1;

    steps[n].name = "colorfill_src"; steps[n++].hr = fill(src, &all, RED);
    steps[n].name = "colorfill_src_block"; steps[n++].hr = fill(src, &blk, BLUE);
    steps[n].name = "colorfill_dst"; steps[n++].hr = fill(dst, &all, BLACK);

    RECT r0 = {0, 0, 256, 64};
    steps[n].name = "copy"; steps[n++].hr = IDirectDrawSurface7_Blt(dst, &r0, src, &r0, DDBLT_WAIT, NULL);

    RECT s1 = {32, 32, 96, 64}, d1 = {0, 64, 128, 128};
    /* src 32..95 x 32..63: blue for x<64, red beyond; stretched 2x. */
    steps[n].hal = stretch_hal; steps[n].name = "stretch"; steps[n++].hr = IDirectDrawSurface7_Blt(dst, &d1, src, &s1, DDBLT_WAIT, NULL);

    RECT d2 = {0, 128, 256, 192};
    fill(dst, &d2, GREEN);
    DDCOLORKEY key = {BLUE, BLUE};
    IDirectDrawSurface7_SetColorKey(src, DDCKEY_SRCBLT, &key);
    RECT s2 = {0, 0, 256, 64};
    steps[n].hal = ckey_hal; steps[n].name = "colorkey_src"; steps[n++].hr = IDirectDrawSurface7_Blt(dst, &d2, src, &s2, DDBLT_WAIT | DDBLT_KEYSRC, NULL);

    RECT sl = {0, 64, 128, 128}, sr = {128, 64, 256, 128};
    fill(src, &sl, WHITE);
    fill(src, &sr, BLACK);
    RECT s3 = {0, 64, 256, 128}, d3 = {0, 192, 256, 256};
    DDBLTFX fx = {0};
    fx.dwSize = sizeof(fx);
    fx.dwDDFX = DDBLTFX_MIRRORLEFTRIGHT;
    steps[n].hal = mirror_hal; steps[n].name = "mirror"; steps[n++].hr = IDirectDrawSurface7_Blt(dst, &d3, src, &s3, DDBLT_WAIT | DDBLT_DDFX, &fx);

    out(", \"steps\": {");
    for (int i = 0; i < n; i++) {
        out("%s\"%s\": {\"hr\": \"0x%08lx\", \"hal\": %s}", i ? ", " : "", steps[i].name,
            (unsigned long)steps[i].hr, steps[i].hal ? "true" : "false");
        ok &= SUCCEEDED(steps[i].hr);
    }
    out("}");

    static const struct check checks[] = {
        {"copy_block", 10, 10, BLUE, 3}, {"copy_red", 200, 30, RED, 3},
        {"stretch_blue", 40, 80, BLUE, 4}, {"stretch_red", 100, 120, RED, 4},
        {"colorkey_keyed", 10, 140, GREEN, 5}, {"colorkey_kept", 200, 140, RED, 5},
        {"mirror_black_left", 10, 220, BLACK, 6}, {"mirror_white_right", 240, 220, WHITE, 6},
    };
    DDSURFACEDESC2 sd = {0};
    sd.dwSize = sizeof(sd);
    hr = IDirectDrawSurface7_Lock(dst, NULL, &sd, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    out(", \"lock_hr\": \"0x%08lx\", \"checks\": {", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
            const DWORD *row = (const DWORD *)((const BYTE *)sd.lpSurface + checks[i].y * sd.lPitch);
            DWORD got = row[checks[i].x] & 0x00ffffff;
            int match = got == checks[i].want;
            int hal_step = steps[checks[i].step].hal;
            ok &= match;
            out("%s\"%s\": {\"want\": \"0x%06lx\", \"got\": \"0x%06lx\", \"ok\": %s, \"hal\": %s}",
                i ? ", " : "", checks[i].what, (unsigned long)checks[i].want,
                (unsigned long)got, match ? "true" : "false", hal_step ? "true" : "false");
        }
        IDirectDrawSurface7_Unlock(dst, NULL);
    } else {
        ok = 0;
    }
    out("}, \"pass\": %s}", ok ? "true" : "false");
    IDirectDrawSurface7_Release(src);
    IDirectDrawSurface7_Release(dst);
    return ok;
}

/* Blits a moving pattern into a window through the primary surface. */
static void window_test(IDirectDraw7 *dd, int seconds)
{
    HRESULT hr;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "ddcaps";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, "ddcaps", "vmlab-win3d probe: DirectDraw blit",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 640, 400,
                                NULL, NULL, wc.hInstance, NULL);
    out("\"window\": {");
    hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    out("\"cooperative_hr\": \"0x%08lx\"", (unsigned long)hr);
    DDSURFACEDESC2 sd = {0};
    sd.dwSize = sizeof(sd);
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDrawSurface7 *primary = NULL;
    hr = IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);
    out(", \"primary_hr\": \"0x%08lx\"", (unsigned long)hr);
    IDirectDrawClipper *clip = NULL;
    if (primary && SUCCEEDED(IDirectDraw7_CreateClipper(dd, 0, &clip, NULL))) {
        IDirectDrawClipper_SetHWnd(clip, 0, hwnd);
        IDirectDrawSurface7_SetClipper(primary, clip);
    }
    IDirectDrawSurface7 *back = make_surface(dd, 512, 320, &hr);
    out(", \"back_hr\": \"0x%08lx\"", (unsigned long)hr);
    DWORD frames = 0, failed = 0;
    HRESULT last = S_OK;
    DWORD end = GetTickCount() + (DWORD)seconds * 1000;
    while (primary && back && GetTickCount() < end) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        RECT all = {0, 0, 512, 320};
        fill(back, &all, 0x00202040);
        int x = (int)(frames * 4 % 448);
        RECT r = {x, 40, x + 64, 104}, g = {448 - x, 140, 512 - x, 204}, b = {x, 240, x + 64, 304};
        fill(back, &r, RED);
        fill(back, &g, GREEN);
        fill(back, &b, BLUE);
        POINT p = {0, 0};
        ClientToScreen(hwnd, &p);
        RECT cr;
        GetClientRect(hwnd, &cr);
        OffsetRect(&cr, p.x, p.y);
        last = IDirectDrawSurface7_Blt(primary, &cr, back, NULL, DDBLT_WAIT, NULL);
        if (FAILED(last))
            failed++;
        frames++;
        Sleep(16);
    }
    out(", \"frames\": %lu, \"failed_blits\": %lu, \"last_hr\": \"0x%08lx\"}",
        (unsigned long)frames, (unsigned long)failed, (unsigned long)last);
    if (back) IDirectDrawSurface7_Release(back);
    if (clip) IDirectDrawClipper_Release(clip);
    if (primary) IDirectDrawSurface7_Release(primary);
    IDirectDraw7_SetCooperativeLevel(dd, NULL, DDSCL_NORMAL);
    DestroyWindow(hwnd);
}

int main(int argc, char **argv)
{
    int seconds = 0;
    const char *outfile = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            outfile = argv[++i];
    }

    IDirectDraw7 *dd = NULL;
    HRESULT hr = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    out("{\"probe\": \"ddcaps\", \"arch\": \"%s\", \"pid\": %lu, \"session_id\": %lu, \"create_hr\": \"0x%08lx\"",
        sizeof(void *) == 8 ? "x64" : "x86", (unsigned long)GetCurrentProcessId(),
        (unsigned long)session, (unsigned long)hr);
    int pass = 0, code = 2;
    if (SUCCEEDED(hr)) {
        DDDEVICEIDENTIFIER2 id = {0};
        if (SUCCEEDED(IDirectDraw7_GetDeviceIdentifier(dd, &id, 0))) {
            out(", \"driver\": ");
            out_str(id.szDriver);
            out(", \"description\": ");
            out_str(id.szDescription);
            out(", \"vendor_id\": \"0x%04lx\", \"device_id\": \"0x%04lx\"",
                (unsigned long)id.dwVendorId, (unsigned long)id.dwDeviceId);
        }
        DDCAPS hal = {0}, hel = {0};
        hal.dwSize = hel.dwSize = sizeof(DDCAPS);
        hr = IDirectDraw7_GetCaps(dd, &hal, &hel);
        out(", \"getcaps_hr\": \"0x%08lx\", ", (unsigned long)hr);
        out_caps("hal", &hal);
        out(", ");
        out_caps("hel", &hel);
        int has_hal = SUCCEEDED(hr) && !(hal.dwCaps & DDCAPS_NOHARDWARE) && (hal.dwCaps & DDCAPS_BLT);
        out(", \"hal_present\": %s, \"nonlocal_vidmem\": %s, \"vidmem\": {",
            has_hal ? "true" : "false",
            (hal.dwCaps2 & DDCAPS2_NONLOCALVIDMEM) ? "true" : "false");
        out_vidmem(dd, "local", DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM);
        out(", ");
        out_vidmem(dd, "nonlocal_texture", DDSCAPS_TEXTURE | DDSCAPS_NONLOCALVIDMEM | DDSCAPS_VIDEOMEMORY);
        out("}, \"d3d7_devices\": [");
        IDirect3D7 *d3d = NULL;
        if (SUCCEEDED(IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d))) {
            IDirect3D7_EnumDevices(d3d, enum_d3d7, NULL);
            IDirect3D7_Release(d3d);
        }
        out("], ");
        IDirectDraw7_SetCooperativeLevel(dd, NULL, DDSCL_NORMAL);
        int blit_ok = blit_test(dd, &hal);
        if (seconds > 0) {
            out(", ");
            window_test(dd, seconds);
        }
        pass = has_hal && blit_ok;
        code = pass ? 0 : 1;
        IDirectDraw7_Release(dd);
    }
    /* "result" is the field tests/guest/Run-Probe.ps1 reads. */
    out(", \"pass\": %s, \"result\": \"%s\"}\n", pass ? "true" : "false", pass ? "pass" : "fail");
    fputs(g_json, stdout);
    if (outfile) {
        FILE *f = fopen(outfile, "wb");
        if (f) {
            fputs(g_json, f);
            fclose(f);
        }
    }
    return code;
}
