/*
 * ddfull: fullscreen exclusive DirectDraw with a flip chain.
 *
 *   ddfull.exe [--modes 800x600x32,640x480x16] [--frames N] [--seconds S]
 *              [--backbuffers B] [--check-every K] [--out FILE]
 *
 * For each mode it takes DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN, calls
 * SetDisplayMode, creates a primary surface with B back buffers (a flip
 * chain), and draws N frames (default 300, and for at least S seconds):
 *   - a colour fill of the whole back buffer, a different colour per frame;
 *   - a 64x64 sprite blitted from a video-memory surface with a source
 *     colour key (its key-coloured border must not show);
 *   - a 16x16 square written on the CPU through Lock;
 *   - Flip, then a read-only Lock of the front buffer and four pixel checks
 *     (background, keyed sprite border, sprite centre, CPU square) against
 *     the frame just drawn, on every frame or every --check-every frames.
 * It lists the modes DirectDraw and EnumDisplaySettings offer first.
 * Then it restores the display mode and checks that the desktop mode is the
 * one it started with.
 *
 * It prints one JSON object (and writes it to FILE). Exit code: 0 when every
 * mode was set, every frame flipped and every pixel check matched, and the
 * desktop mode came back; 1 otherwise; 2 when DirectDraw could not be
 * created.
 */
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_json[262144];
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

struct pixfmt { DWORD bits, r, g, b; };

static int mask_shift(DWORD m)
{
    int s = 0;
    if (!m)
        return 0;
    while (!(m & 1)) {
        m >>= 1;
        s++;
    }
    return s;
}

static DWORD mask_max(DWORD m) { return m >> mask_shift(m); }

/* Packs an 0xRRGGBB colour into the surface format. The test colours use
 * only 0x00 and 0xff per channel, so they survive any channel depth. */
static DWORD pack(const struct pixfmt *f, DWORD rgb)
{
    DWORD r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    return ((r * mask_max(f->r) / 255) << mask_shift(f->r)) |
           ((g * mask_max(f->g) / 255) << mask_shift(f->g)) |
           ((b * mask_max(f->b) / 255) << mask_shift(f->b));
}

static DWORD read_px(const DDSURFACEDESC2 *sd, const struct pixfmt *f, int x, int y)
{
    const BYTE *row = (const BYTE *)sd->lpSurface + y * sd->lPitch;
    if (f->bits == 16)
        return ((const WORD *)row)[x];
    return ((const DWORD *)row)[x] & (f->r | f->g | f->b);
}

static void write_px(const DDSURFACEDESC2 *sd, const struct pixfmt *f, int x, int y, DWORD v)
{
    BYTE *row = (BYTE *)sd->lpSurface + y * sd->lPitch;
    if (f->bits == 16)
        ((WORD *)row)[x] = (WORD)v;
    else
        ((DWORD *)row)[x] = v;
}

static HRESULT fill(IDirectDrawSurface7 *s, RECT *r, DWORD native)
{
    DDBLTFX fx = {0};
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = native;
    return IDirectDrawSurface7_Blt(s, r, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
}

static const DWORD frame_colours[] = {
    0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff,
};
#define KEY 0xff00ff    /* sprite border, keyed out */
#define SPRITE 0xffffff /* sprite centre */
#define CPU 0x000000    /* square written through Lock */

static int g_nmodes;
static HRESULT WINAPI enum_mode(LPDDSURFACEDESC2 sd, LPVOID ctx)
{
    out("%s\"%lux%lux%lu\"", g_nmodes++ ? ", " : "", (unsigned long)sd->dwWidth,
        (unsigned long)sd->dwHeight, (unsigned long)sd->ddpfPixelFormat.dwRGBBitCount);
    return DDENUMRET_OK;
}

static void current_mode(DEVMODEA *dm)
{
    memset(dm, 0, sizeof(*dm));
    dm->dmSize = sizeof(*dm);
    EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, dm);
}

/* Runs one mode. Returns 1 when everything matched. */
static int run_mode(IDirectDraw7 *dd, HWND hwnd, DWORD w, DWORD h, DWORD bpp,
                    int frames, int seconds, int backbuffers, int check_every)
{
    HRESULT hr;
    int ok = 1;
    out("{\"mode\": \"%lux%lux%lu\"", (unsigned long)w, (unsigned long)h, (unsigned long)bpp);
    hr = IDirectDraw7_SetDisplayMode(dd, w, h, bpp, 0, 0);
    out(", \"set_display_mode_hr\": \"0x%08lx\"", (unsigned long)hr);
    if (FAILED(hr)) {
        out(", \"pass\": false}");
        return 0;
    }
    DEVMODEA dm;
    current_mode(&dm);
    out(", \"os_mode\": \"%lux%lux%lu@%lu\"", (unsigned long)dm.dmPelsWidth,
        (unsigned long)dm.dmPelsHeight, (unsigned long)dm.dmBitsPerPel,
        (unsigned long)dm.dmDisplayFrequency);
    ok &= dm.dmPelsWidth == w && dm.dmPelsHeight == h;
    DDSURFACEDESC2 mode = {0};
    mode.dwSize = sizeof(mode);
    hr = IDirectDraw7_GetDisplayMode(dd, &mode);
    out(", \"ddraw_mode\": \"%lux%lux%lu@%lu\"", (unsigned long)mode.dwWidth,
        (unsigned long)mode.dwHeight, (unsigned long)mode.ddpfPixelFormat.dwRGBBitCount,
        (unsigned long)mode.dwRefreshRate);
    ok &= SUCCEEDED(hr) && mode.dwWidth == w && mode.dwHeight == h &&
          mode.ddpfPixelFormat.dwRGBBitCount == bpp;

    DDSURFACEDESC2 sd = {0};
    sd.dwSize = sizeof(sd);
    sd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    sd.dwBackBufferCount = (DWORD)backbuffers;
    IDirectDrawSurface7 *primary = NULL, *back = NULL, *sprite = NULL;
    hr = IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);
    out(", \"primary_hr\": \"0x%08lx\"", (unsigned long)hr);
    if (FAILED(hr)) {
        out(", \"pass\": false}");
        return 0;
    }
    DDSCAPS2 bc = {0};
    bc.dwCaps = DDSCAPS_BACKBUFFER;
    hr = IDirectDrawSurface7_GetAttachedSurface(primary, &bc, &back);
    out(", \"backbuffer_hr\": \"0x%08lx\"", (unsigned long)hr);
    DDSCAPS2 pc = {0};
    IDirectDrawSurface7_GetCaps(primary, &pc);
    out(", \"primary_caps\": \"0x%08lx\"", (unsigned long)pc.dwCaps);

    DDPIXELFORMAT pf = {0};
    pf.dwSize = sizeof(pf);
    IDirectDrawSurface7_GetPixelFormat(primary, &pf);
    struct pixfmt f = {pf.dwRGBBitCount, pf.dwRBitMask, pf.dwGBitMask, pf.dwBBitMask};
    out(", \"pixel_format\": {\"bits\": %lu, \"r\": \"0x%08lx\", \"g\": \"0x%08lx\", \"b\": \"0x%08lx\"}",
        (unsigned long)f.bits, (unsigned long)f.r, (unsigned long)f.g, (unsigned long)f.b);

    /* The sprite: a white 32x32 centre in a 64x64 key-coloured square. */
    DDSURFACEDESC2 ss = {0};
    ss.dwSize = sizeof(ss);
    ss.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ss.dwWidth = ss.dwHeight = 64;
    ss.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw7_CreateSurface(dd, &ss, &sprite, NULL);
    out(", \"sprite_hr\": \"0x%08lx\"", (unsigned long)hr);
    if (sprite) {
        RECT all = {0, 0, 64, 64}, mid = {16, 16, 48, 48};
        fill(sprite, &all, pack(&f, KEY));
        fill(sprite, &mid, pack(&f, SPRITE));
        DDCOLORKEY key = {pack(&f, KEY), pack(&f, KEY)};
        IDirectDrawSurface7_SetColorKey(sprite, DDCKEY_SRCBLT, &key);
    }

    int flipped = 0, flip_failed = 0, blit_failed = 0, lock_failed = 0;
    int checked = 0, mismatched = 0;
    HRESULT last_flip = S_OK;
    char first_bad[256] = "";
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    DWORD end = GetTickCount() + (DWORD)seconds * 1000;
    for (int i = 0; primary && back && (i < frames || GetTickCount() < end); i++) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        const DWORD bg = frame_colours[i % 6];
        RECT all = {0, 0, (LONG)w, (LONG)h};
        if (FAILED(fill(back, &all, pack(&f, bg))))
            blit_failed++;
        /* Three colour bars that move, so the console shows motion. */
        for (int b = 0; b < 3; b++) {
            LONG x = (LONG)((i * 6 + b * 200) % (w - 80));
            RECT bar = {x, (LONG)h / 2 + b * 40, x + 80, (LONG)h / 2 + b * 40 + 30};
            fill(back, &bar, pack(&f, frame_colours[(i + b + 1) % 6]));
        }
        const LONG sx = 100, sy = 100;
        if (sprite) {
            RECT src = {0, 0, 64, 64}, dst = {sx, sy, sx + 64, sy + 64};
            if (FAILED(IDirectDrawSurface7_Blt(back, &dst, sprite, &src, DDBLT_WAIT | DDBLT_KEYSRC, NULL)))
                blit_failed++;
        }
        DDSURFACEDESC2 ld = {0};
        ld.dwSize = sizeof(ld);
        RECT sq = {200, 100, 216, 116};
        if (SUCCEEDED(IDirectDrawSurface7_Lock(back, &sq, &ld, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL))) {
            /* With a rectangle, lpSurface points at its top-left pixel. */
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    write_px(&ld, &f, x, y, pack(&f, CPU));
            IDirectDrawSurface7_Unlock(back, &sq);
        } else {
            lock_failed++;
        }
        last_flip = IDirectDrawSurface7_Flip(primary, NULL, DDFLIP_WAIT);
        if (FAILED(last_flip)) {
            flip_failed++;
            continue;
        }
        flipped++;
        if (i % check_every)
            continue;
        ld.dwSize = sizeof(ld);
        if (SUCCEEDED(IDirectDrawSurface7_Lock(primary, NULL, &ld, DDLOCK_WAIT | DDLOCK_READONLY, NULL))) {
            struct { const char *what; int x, y; DWORD want; } c[] = {
                {"background", 20, 20, bg},
                {"sprite_keyed", (int)sx + 4, (int)sy + 4, bg},
                {"sprite_centre", (int)sx + 32, (int)sy + 32, SPRITE},
                {"cpu_square", 208, 108, CPU},
            };
            for (int k = 0; k < 4; k++) {
                DWORD got = read_px(&ld, &f, c[k].x, c[k].y), want = pack(&f, c[k].want);
                checked++;
                if (got != want) {
                    if (!mismatched++)
                        snprintf(first_bad, sizeof(first_bad),
                                 "frame %d %s at %d,%d: want 0x%08lx got 0x%08lx", i, c[k].what,
                                 c[k].x, c[k].y, (unsigned long)want, (unsigned long)got);
                }
            }
            IDirectDrawSurface7_Unlock(primary, NULL);
        } else {
            lock_failed++;
        }
    }
    QueryPerformanceCounter(&t1);
    double secs = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    out(", \"frames_flipped\": %d, \"flip_failed\": %d, \"last_flip_hr\": \"0x%08lx\", "
        "\"blit_failed\": %d, \"lock_failed\": %d, \"pixel_checks\": %d, \"pixel_mismatches\": %d, "
        "\"first_mismatch\": \"%s\", \"seconds\": %.2f, \"fps\": %.1f",
        flipped, flip_failed, (unsigned long)last_flip, blit_failed, lock_failed, checked,
        mismatched, first_bad, secs, secs > 0 ? flipped / secs : 0.0);
    ok &= flipped >= frames && !flip_failed && !blit_failed && !lock_failed && !mismatched &&
          checked == (flipped + check_every - 1) / check_every * 4;
    if (sprite) IDirectDrawSurface7_Release(sprite);
    if (back) IDirectDrawSurface7_Release(back);
    IDirectDrawSurface7_Release(primary);
    out(", \"pass\": %s}", ok ? "true" : "false");
    return ok;
}

int main(int argc, char **argv)
{
    const char *modes = "800x600x32,640x480x16";
    const char *outfile = NULL;
    int frames = 300, seconds = 0, backbuffers = 1, check_every = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--modes") && i + 1 < argc)
            modes = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--backbuffers") && i + 1 < argc)
            backbuffers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--check-every") && i + 1 < argc)
            check_every = atoi(argv[++i]) > 0 ? atoi(argv[i]) : 1;
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            outfile = argv[++i];
    }

    DEVMODEA before;
    current_mode(&before);
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    IDirectDraw7 *dd = NULL;
    HRESULT hr = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    out("{\"probe\": \"ddfull\", \"arch\": \"%s\", \"session_id\": %lu, \"create_hr\": \"0x%08lx\", "
        "\"desktop_before\": \"%lux%lux%lu@%lu\", \"backbuffers\": %d, \"frames\": %d, \"check_every\": %d",
        sizeof(void *) == 8 ? "x64" : "x86", (unsigned long)session, (unsigned long)hr,
        (unsigned long)before.dmPelsWidth, (unsigned long)before.dmPelsHeight,
        (unsigned long)before.dmBitsPerPel, (unsigned long)before.dmDisplayFrequency,
        backbuffers, frames, check_every);
    int pass = 0, code = 2;
    if (SUCCEEDED(hr)) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "ddfull";
        wc.hCursor = NULL;
        RegisterClassA(&wc);
        HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, "ddfull", "vmlab-win3d probe: DirectDraw fullscreen",
                                    WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                                    GetSystemMetrics(SM_CYSCREEN), NULL, NULL, wc.hInstance, NULL);
        SetForegroundWindow(hwnd);
        out(", \"ddraw_modes\": [");
        IDirectDraw7_EnumDisplayModes(dd, 0, NULL, NULL, enum_mode);
        out("], \"os_modes\": [");
        DEVMODEA em;
        int nos = 0;
        for (DWORD i = 0;; i++) {
            memset(&em, 0, sizeof(em));
            em.dmSize = sizeof(em);
            if (!EnumDisplaySettingsA(NULL, i, &em))
                break;
            out("%s\"%lux%lux%lu@%lu\"", nos++ ? ", " : "", (unsigned long)em.dmPelsWidth,
                (unsigned long)em.dmPelsHeight, (unsigned long)em.dmBitsPerPel,
                (unsigned long)em.dmDisplayFrequency);
        }
        out("]");
        hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
        out(", \"cooperative_hr\": \"0x%08lx\", \"modes\": [", (unsigned long)hr);
        pass = SUCCEEDED(hr);
        char list[256];
        snprintf(list, sizeof(list), "%s", modes);
        int n = 0;
        for (char *tok = strtok(list, ","); tok && SUCCEEDED(hr); tok = strtok(NULL, ",")) {
            unsigned long w = 0, h = 0, bpp = 0;
            if (sscanf(tok, "%lux%lux%lu", &w, &h, &bpp) != 3)
                continue;
            if (n++)
                out(", ");
            pass &= run_mode(dd, hwnd, w, h, bpp, frames, seconds, backbuffers, check_every);
        }
        out("]");
        hr = IDirectDraw7_RestoreDisplayMode(dd);
        out(", \"restore_hr\": \"0x%08lx\"", (unsigned long)hr);
        pass &= SUCCEEDED(hr);
        IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
        IDirectDraw7_Release(dd);
        DestroyWindow(hwnd);
        Sleep(500);
        DEVMODEA after;
        current_mode(&after);
        int same = after.dmPelsWidth == before.dmPelsWidth && after.dmPelsHeight == before.dmPelsHeight &&
                   after.dmBitsPerPel == before.dmBitsPerPel;
        out(", \"desktop_after\": \"%lux%lux%lu@%lu\", \"desktop_restored\": %s",
            (unsigned long)after.dmPelsWidth, (unsigned long)after.dmPelsHeight,
            (unsigned long)after.dmBitsPerPel, (unsigned long)after.dmDisplayFrequency,
            same ? "true" : "false");
        pass &= same;
        code = pass ? 0 : 1;
    }
    /* "result" is the field tests/guest/Run-Probe.ps1 reads. */
    out(", \"pass\": %s, \"result\": \"%s\"}\n", pass ? "true" : "false", pass ? "pass" : "fail");
    fputs(g_json, stdout);
    if (outfile) {
        FILE *fp = fopen(outfile, "wb");
        if (fp) {
            fputs(g_json, fp);
            fclose(fp);
        }
    }
    return code;
}
