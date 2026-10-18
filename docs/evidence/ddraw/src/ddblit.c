/*
 * ddblit: DirectDraw source colour-key and mirror blits on the driver (HAL)
 * against the software emulation layer (HEL).
 *
 *   ddblit.exe [--out FILE] [--timing N]
 *
 * One DirectDraw object and two sets of surfaces: in DDSCAPS_VIDEOMEMORY
 * (HAL) and in DDSCAPS_SYSTEMMEMORY, which DirectDraw blits on the CPU (HEL,
 * as the HAL does not report DDCAPS_CANBLTSYSMEM). For each pixel format (X8R8G8B8, R5G6B5,
 * A8R8G8B8) and each case (colour key at 1:1, stretched, shrunk and odd
 * ratios, mirror left-right, up-down and both, mirror with stretch, mirror
 * with colour key), both devices start from the same source and destination
 * contents, written through Lock, run the same Blt, and the whole
 * destination is read back through Lock and compared pixel by pixel.
 * "hal_caps" says whether the HAL advertises the operation; when it does
 * not, DirectDraw emulates it on the CPU on the video-memory surfaces, and
 * the comparison still runs.
 *
 * --timing N times N 256x256 colour-key blits on each device.
 * It prints one JSON object (and writes it to FILE). Exit code: 0 when every
 * HAL result matches the HEL result exactly; 1 otherwise; 2 when DirectDraw
 * could not be created.
 */
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_json[131072];
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

struct format {
    const char *name;
    DWORD bits, flags, r, g, b, a;
    DWORD key; /* a pixel value the source pattern uses often */
    DWORD compare_mask;
};

static const struct format formats[] = {
    {"X8R8G8B8", 32, DDPF_RGB, 0xff0000, 0x00ff00, 0x0000ff, 0, 0x00ff00ff, 0x00ffffff},
    {"R5G6B5", 16, DDPF_RGB, 0xf800, 0x07e0, 0x001f, 0, 0xf81f, 0xffff},
    {"A8R8G8B8", 32, DDPF_RGB | DDPF_ALPHAPIXELS, 0xff0000, 0x00ff00, 0x0000ff, 0xff000000,
     0x80ff00ff, 0xffffffff},
};

#define SIZE 128

static void finish(const char *outfile)
{
    fputs(g_json, stdout);
    if (outfile) {
        FILE *fp = fopen(outfile, "wb");
        if (fp) {
            fputs(g_json, fp);
            fclose(fp);
        }
    }
}

static IDirectDrawSurface7 *make_surface(IDirectDraw7 *dd, const struct format *f, DWORD w, DWORD h,
                                         DWORD memcaps, HRESULT *hr)
{
    DDSURFACEDESC2 sd = {0};
    IDirectDrawSurface7 *s = NULL;
    sd.dwSize = sizeof(sd);
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    sd.dwWidth = w;
    sd.dwHeight = h;
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | memcaps;
    sd.ddpfPixelFormat.dwSize = sizeof(sd.ddpfPixelFormat);
    sd.ddpfPixelFormat.dwFlags = f->flags;
    sd.ddpfPixelFormat.dwRGBBitCount = f->bits;
    sd.ddpfPixelFormat.dwRBitMask = f->r;
    sd.ddpfPixelFormat.dwGBitMask = f->g;
    sd.ddpfPixelFormat.dwBBitMask = f->b;
    sd.ddpfPixelFormat.dwRGBAlphaBitMask = f->a;
    *hr = IDirectDraw7_CreateSurface(dd, &sd, &s, NULL);
    return SUCCEEDED(*hr) ? s : NULL;
}

static DWORD hash(DWORD x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* Source: the key in a checker of 8x8 cells plus scattered pixels, other
 * pixels pseudo-random. For A8R8G8B8, some pixels have the key's colour
 * with a different alpha, to show whether alpha takes part in the match. */
static DWORD src_pixel(const struct format *f, int x, int y)
{
    DWORD h = hash((DWORD)(y * 977 + x) ^ 0x1234);
    if ((((x >> 3) ^ (y >> 3)) & 1) || (h & 7) == 0)
        return f->key;
    if (f->a && (h & 7) == 1)
        return (f->key & 0x00ffffff) | 0x10000000;
    DWORD v = hash(h);
    if (f->bits == 16)
        v &= 0xffff;
    if (v == f->key)
        v ^= 1;
    return v;
}

static DWORD dst_pixel(const struct format *f, int x, int y)
{
    DWORD v = hash((DWORD)(y * 131 + x) ^ 0xabcdef);
    return f->bits == 16 ? v & 0xffff : v;
}

static HRESULT write_surface(IDirectDrawSurface7 *s, const struct format *f, int src)
{
    DDSURFACEDESC2 sd = {0};
    sd.dwSize = sizeof(sd);
    HRESULT hr = IDirectDrawSurface7_Lock(s, NULL, &sd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (FAILED(hr))
        return hr;
    for (int y = 0; y < SIZE; y++) {
        BYTE *row = (BYTE *)sd.lpSurface + y * sd.lPitch;
        for (int x = 0; x < SIZE; x++) {
            DWORD v = src ? src_pixel(f, x, y) : dst_pixel(f, x, y);
            if (f->bits == 16)
                ((WORD *)row)[x] = (WORD)v;
            else
                ((DWORD *)row)[x] = v;
        }
    }
    return IDirectDrawSurface7_Unlock(s, NULL);
}

static HRESULT read_surface(IDirectDrawSurface7 *s, const struct format *f, DWORD *px)
{
    DDSURFACEDESC2 sd = {0};
    sd.dwSize = sizeof(sd);
    HRESULT hr = IDirectDrawSurface7_Lock(s, NULL, &sd, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (FAILED(hr))
        return hr;
    for (int y = 0; y < SIZE; y++) {
        const BYTE *row = (const BYTE *)sd.lpSurface + y * sd.lPitch;
        for (int x = 0; x < SIZE; x++)
            px[y * SIZE + x] =
                (f->bits == 16 ? ((const WORD *)row)[x] : ((const DWORD *)row)[x]) & f->compare_mask;
    }
    return IDirectDrawSurface7_Unlock(s, NULL);
}

struct test {
    const char *name;
    RECT src, dst;
    DWORD flags, fx;
};

static const struct test tests[] = {
    {"copy", {0, 0, 64, 64}, {10, 10, 74, 74}, 0, 0},
    {"colorkey", {0, 0, 64, 64}, {10, 10, 74, 74}, DDBLT_KEYSRC, 0},
    {"colorkey_full", {0, 0, SIZE, SIZE}, {0, 0, SIZE, SIZE}, DDBLT_KEYSRC, 0},
    {"colorkey_stretch2x", {8, 8, 40, 40}, {20, 20, 84, 84}, DDBLT_KEYSRC, 0},
    {"colorkey_shrink2x", {0, 0, 128, 128}, {30, 30, 94, 94}, DDBLT_KEYSRC, 0},
    {"colorkey_odd", {3, 5, 40, 34}, {7, 2, 98, 55}, DDBLT_KEYSRC, 0},
    {"mirror_lr", {0, 0, 64, 64}, {10, 10, 74, 74}, DDBLT_DDFX, DDBLTFX_MIRRORLEFTRIGHT},
    {"mirror_ud", {0, 0, 64, 64}, {10, 10, 74, 74}, DDBLT_DDFX, DDBLTFX_MIRRORUPDOWN},
    {"mirror_both", {5, 9, 69, 57}, {50, 60, 114, 108}, DDBLT_DDFX,
     DDBLTFX_MIRRORLEFTRIGHT | DDBLTFX_MIRRORUPDOWN},
    {"mirror_lr_stretch2x", {8, 8, 40, 40}, {20, 20, 84, 84}, DDBLT_DDFX, DDBLTFX_MIRRORLEFTRIGHT},
    {"mirror_lr_colorkey", {0, 0, 64, 64}, {10, 10, 74, 74}, DDBLT_DDFX | DDBLT_KEYSRC,
     DDBLTFX_MIRRORLEFTRIGHT},
    {"mirror_ud_colorkey_odd", {3, 5, 40, 34}, {7, 2, 98, 55}, DDBLT_DDFX | DDBLT_KEYSRC,
     DDBLTFX_MIRRORUPDOWN},
    {"colorkey_same_surface", {0, 0, 32, 32}, {64, 64, 96, 96}, DDBLT_KEYSRC, 0},
};

struct dev {
    IDirectDraw7 *dd;
    DWORD memcaps;
    IDirectDrawSurface7 *src, *dst;
};

static HRESULT run_blit(struct dev *d, const struct format *f, const struct test *t, DWORD *px)
{
    HRESULT hr = write_surface(d->src, f, 1);
    if (SUCCEEDED(hr))
        hr = write_surface(d->dst, f, 0);
    if (FAILED(hr))
        return hr;
    DDCOLORKEY key = {f->key, f->key};
    IDirectDrawSurface7_SetColorKey(d->src, DDCKEY_SRCBLT, &key);
    DDBLTFX fx = {0};
    fx.dwSize = sizeof(fx);
    fx.dwDDFX = t->fx;
    RECT s = t->src, ds = t->dst;
    int same = strstr(t->name, "same_surface") != NULL;
    hr = IDirectDrawSurface7_Blt(same ? d->src : d->dst, &ds, d->src, &s, DDBLT_WAIT | t->flags,
                                 t->flags & DDBLT_DDFX ? &fx : NULL);
    if (FAILED(hr))
        return hr;
    return read_surface(same ? d->src : d->dst, f, px);
}

static double time_blits(struct dev *d, const struct format *f, int n, int *failed)
{
    LARGE_INTEGER freq, t0, t1;
    RECT r = {0, 0, SIZE, SIZE};
    static DWORD px[SIZE * SIZE];
    write_surface(d->src, f, 1);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    *failed = 0;
    for (int i = 0; i < n; i++)
        *failed += FAILED(IDirectDrawSurface7_Blt(d->dst, &r, d->src, &r, DDBLT_WAIT | DDBLT_KEYSRC, NULL));
    read_surface(d->dst, f, px);
    QueryPerformanceCounter(&t1);
    return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
}

/* --map: which source row and column each destination pixel of an odd-ratio
 * blit (37x29 -> 91x53) takes, on the HEL, plain and mirrored. */
static void map_diag(struct dev *d, int memfmt)
{
    const struct format *f = &formats[memfmt];
    static const DWORD fx[] = {0, DDBLTFX_MIRRORLEFTRIGHT | DDBLTFX_MIRRORUPDOWN,
                               DDBLTFX_MIRRORUPDOWN, DDBLTFX_MIRRORLEFTRIGHT};
    static const char *names[] = {"plain", "mirror_both", "mirror_ud", "mirror_lr"};
    static DWORD px[SIZE * SIZE];
    out(", \"map\": {");
    for (int m = 0; m < 4; m++) {
        DDSURFACEDESC2 sd = {0};
        sd.dwSize = sizeof(sd);
        if (FAILED(IDirectDrawSurface7_Lock(d->src, NULL, &sd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL)))
            return;
        for (int y = 0; y < SIZE; y++)
            for (int x = 0; x < SIZE; x++)
                ((DWORD *)((BYTE *)sd.lpSurface + y * sd.lPitch))[x] = (DWORD)(y << 8 | x);
        IDirectDrawSurface7_Unlock(d->src, NULL);
        RECT s = {3, 5, 40, 34}, ds = {7, 2, 98, 55};
        DDBLTFX bfx = {0};
        bfx.dwSize = sizeof(bfx);
        bfx.dwDDFX = fx[m];
        IDirectDrawSurface7_Blt(d->dst, &ds, d->src, &s, DDBLT_WAIT | (m ? DDBLT_DDFX : 0), m ? &bfx : NULL);
        read_surface(d->dst, f, px);
        out("%s\"%s\": {\"rows\": [", m ? ", " : "", names[m]);
        for (int y = 2; y < 55; y++)
            out("%s%lu", y > 2 ? "," : "", (unsigned long)(px[y * SIZE + 7] >> 8 & 0xff));
        out("], \"cols\": [");
        for (int x = 7; x < 98; x++)
            out("%s%lu", x > 7 ? "," : "", (unsigned long)(px[2 * SIZE + x] & 0xff));
        out("]}");
    }
    out("}");
}

int main(int argc, char **argv)
{
    const char *outfile = NULL;
    int timing = 0, map = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)
            outfile = argv[++i];
        else if (!strcmp(argv[i], "--map"))
            map = 1;
        else if (!strcmp(argv[i], "--timing") && i + 1 < argc)
            timing = atoi(argv[++i]);
    }
    struct dev hal = {NULL, DDSCAPS_VIDEOMEMORY}, hel = {NULL, DDSCAPS_SYSTEMMEMORY};
    /* DDCREATE_EMULATIONONLY fails (E_FAIL) on Windows 11, so the HEL
     * reference is the same object's system-memory surfaces: DirectDraw blits
     * between them on the CPU because the HAL has no DDCAPS_CANBLTSYSMEM. */
    HRESULT hr1 = DirectDrawCreateEx(NULL, (void **)&hal.dd, &IID_IDirectDraw7, NULL);
    HRESULT hr2 = hr1;
    hel.dd = hal.dd;
    out("{\"probe\": \"ddblit\", \"arch\": \"%s\", \"hal_create_hr\": \"0x%08lx\", \"hel_create_hr\": \"0x%08lx\"",
        sizeof(void *) == 8 ? "x64" : "x86", (unsigned long)hr1, (unsigned long)hr2);
    if (FAILED(hr1) || FAILED(hr2)) {
        out(", \"pass\": false, \"result\": \"fail\"}\n");
        finish(outfile);
        return 2;
    }
    IDirectDraw7_SetCooperativeLevel(hal.dd, NULL, DDSCL_NORMAL);
    DDCAPS caps = {0}, helcaps = {0};
    caps.dwSize = helcaps.dwSize = sizeof(DDCAPS);
    IDirectDraw7_GetCaps(hal.dd, &caps, &helcaps);
    const int hal_ckey = (caps.dwCKeyCaps & DDCKEYCAPS_SRCBLT) && (caps.dwCaps & DDCAPS_COLORKEY);
    const int hal_mlr = (caps.dwFXCaps & DDFXCAPS_BLTMIRRORLEFTRIGHT) != 0;
    const int hal_mud = (caps.dwFXCaps & DDFXCAPS_BLTMIRRORUPDOWN) != 0;
    out(", \"hal_dwCaps\": \"0x%08lx\", \"hal_dwCKeyCaps\": \"0x%08lx\", \"hal_dwFXCaps\": \"0x%08lx\", "
        "\"hal_colorkey_srcblt\": %s, \"hal_mirror_lr\": %s, \"hal_mirror_ud\": %s, \"formats\": [",
        (unsigned long)caps.dwCaps, (unsigned long)caps.dwCKeyCaps, (unsigned long)caps.dwFXCaps,
        hal_ckey ? "true" : "false", hal_mlr ? "true" : "false", hal_mud ? "true" : "false");

    static DWORD a[SIZE * SIZE], b[SIZE * SIZE];
    int pass = 1;
    for (size_t fi = 0; fi < sizeof(formats) / sizeof(formats[0]); fi++) {
        const struct format *f = &formats[fi];
        HRESULT h1, h2, h3, h4;
        hal.src = make_surface(hal.dd, f, SIZE, SIZE, hal.memcaps, &h1);
        hal.dst = make_surface(hal.dd, f, SIZE, SIZE, hal.memcaps, &h2);
        hel.src = make_surface(hel.dd, f, SIZE, SIZE, hel.memcaps, &h3);
        hel.dst = make_surface(hel.dd, f, SIZE, SIZE, hel.memcaps, &h4);
        out("%s{\"format\": \"%s\", \"create_hr\": [\"0x%08lx\", \"0x%08lx\", \"0x%08lx\", \"0x%08lx\"]",
            fi ? ", " : "", f->name, (unsigned long)h1, (unsigned long)h2, (unsigned long)h3,
            (unsigned long)h4);
        int fok = hal.src && hal.dst && hel.src && hel.dst;
        if (hal.dst) {
            DDSCAPS2 sc = {0};
            IDirectDrawSurface7_GetCaps(hal.dst, &sc);
            out(", \"hal_surface_in_vidmem\": %s", (sc.dwCaps & DDSCAPS_VIDEOMEMORY) ? "true" : "false");
            fok &= (sc.dwCaps & DDSCAPS_VIDEOMEMORY) != 0;
        }
        out(", \"cases\": {");
        const int created = fok;
        for (size_t ti = 0; created && ti < sizeof(tests) / sizeof(tests[0]); ti++) {
            const struct test *t = &tests[ti];
            int needs_ckey = (t->flags & DDBLT_KEYSRC) != 0;
            int needs_lr = (t->fx & DDBLTFX_MIRRORLEFTRIGHT) != 0;
            int needs_ud = (t->fx & DDBLTFX_MIRRORUPDOWN) != 0;
            int advertised = (!needs_ckey || hal_ckey) && (!needs_lr || hal_mlr) && (!needs_ud || hal_mud);
            HRESULT ha = run_blit(&hal, f, t, a);
            HRESULT hb = run_blit(&hel, f, t, b);
            int mismatches = 0, first = -1;
            for (int i = 0; i < SIZE * SIZE; i++)
                if (a[i] != b[i]) {
                    if (first < 0)
                        first = i;
                    mismatches++;
                }
            int ok = SUCCEEDED(ha) && SUCCEEDED(hb) && !mismatches;
            out("%s\"%s\": {\"hal_caps\": %s, \"hal_hr\": \"0x%08lx\", \"hel_hr\": \"0x%08lx\", "
                "\"mismatches\": %d",
                ti ? ", " : "", t->name, advertised ? "true" : "false", (unsigned long)ha,
                (unsigned long)hb, mismatches);
            if (first >= 0)
                out(", \"first\": {\"x\": %d, \"y\": %d, \"hal\": \"0x%08lx\", \"hel\": \"0x%08lx\"}",
                    first % SIZE, first / SIZE, (unsigned long)a[first], (unsigned long)b[first]);
            out(", \"ok\": %s}", ok ? "true" : "false");
            fok &= ok;
        }
        out("}");
        if (fok && timing > 0) {
            int fh, fe;
            double th = time_blits(&hal, f, timing, &fh), te = time_blits(&hel, f, timing, &fe);
            out(", \"timing\": {\"blits\": %d, \"hal_ms\": %.1f, \"hel_ms\": %.1f, \"hal_failed\": %d, "
                "\"hel_failed\": %d}", timing, th, te, fh, fe);
        }
        if (map && fi == 0 && created) {
            map_diag(&hel, 0);
            out(", \"hal\": {\"x\": 0");
            map_diag(&hal, 0);
            out("}");
        }
        out(", \"pass\": %s}", fok ? "true" : "false");
        pass &= fok;
        if (hal.src) IDirectDrawSurface7_Release(hal.src);
        if (hal.dst) IDirectDrawSurface7_Release(hal.dst);
        if (hel.src) IDirectDrawSurface7_Release(hel.src);
        if (hel.dst) IDirectDrawSurface7_Release(hel.dst);
    }
    out("]");
    IDirectDraw7_Release(hal.dd);
    /* "result" is the field tests/guest/Run-Probe.ps1 reads. */
    out(", \"pass\": %s, \"result\": \"%s\"}\n", pass ? "true" : "false", pass ? "pass" : "fail");
    finish(outfile);
    return pass ? 0 : 1;
}
