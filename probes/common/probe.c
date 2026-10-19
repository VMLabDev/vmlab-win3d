/* Shared harness for the vmlab-win3d probes. See probe.h. */
#define COBJMACROS
#include "probe.h"
#include "font.h"

#include <d3dcommon.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const float probe_clear_color[4] = {0.08f, 0.10f, 0.16f, 1.0f};

/* ---- JSON fact store -------------------------------------------------- */

typedef struct fact {
    probe_section section;
    char key[48];
    int is_list;
    char *json; /* encoded value; for a list, the comma-joined items */
} fact;

static fact g_facts[128];
static int g_nfacts;
static char *g_errors[64];
static int g_nerrors;
static int g_rb_got[PROBE_MARKERS];
static uint8_t g_rb[PROBE_MARKERS][3];

static char *json_escape(const char *s)
{
    size_t n = strlen(s), o = 0;
    char *out = (char *)malloc(n * 6 + 3);
    out[o++] = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (c == '\r') {
            out[o++] = '\\';
            out[o++] = 'r';
        } else if (c == '\t') {
            out[o++] = '\\';
            out[o++] = 't';
        } else if (c < 0x20) {
            o += (size_t)sprintf(out + o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o++] = '"';
    out[o] = 0;
    return out;
}

static fact *fact_slot(probe_section s, const char *key)
{
    for (int i = 0; i < g_nfacts; i++)
        if (g_facts[i].section == s && !strcmp(g_facts[i].key, key))
            return &g_facts[i];
    if (g_nfacts == (int)(sizeof g_facts / sizeof g_facts[0]))
        return NULL;
    fact *f = &g_facts[g_nfacts++];
    memset(f, 0, sizeof *f);
    f->section = s;
    snprintf(f->key, sizeof f->key, "%s", key);
    return f;
}

static void fact_set(probe_section s, const char *key, char *json)
{
    fact *f = fact_slot(s, key);
    if (!f) {
        free(json);
        return;
    }
    free(f->json);
    f->json = json;
    f->is_list = 0;
}

void probe_str(probe_ctx *ctx, probe_section s, const char *key, const char *value)
{
    (void)ctx;
    fact_set(s, key, json_escape(value ? value : ""));
}

void probe_wstr(probe_ctx *ctx, probe_section s, const char *key, const wchar_t *value)
{
    probe_str(ctx, s, key, probe_utf8(value ? value : L""));
}

void probe_int(probe_ctx *ctx, probe_section s, const char *key, long long value)
{
    (void)ctx;
    char *b = (char *)malloc(32);
    snprintf(b, 32, "%lld", value);
    fact_set(s, key, b);
}

void probe_hex(probe_ctx *ctx, probe_section s, const char *key, unsigned value)
{
    (void)ctx;
    char *b = (char *)malloc(16);
    snprintf(b, 16, "\"0x%04x\"", value);
    fact_set(s, key, b);
}

void probe_bool(probe_ctx *ctx, probe_section s, const char *key, int value)
{
    (void)ctx;
    char *b = (char *)malloc(8);
    snprintf(b, 8, "%s", value ? "true" : "false");
    fact_set(s, key, b);
}

void probe_list_add(probe_ctx *ctx, probe_section s, const char *key, const char *value)
{
    (void)ctx;
    fact *f = fact_slot(s, key);
    if (!f)
        return;
    char *item = json_escape(value ? value : "");
    if (!f->is_list) {
        free(f->json);
        f->json = item;
        f->is_list = 1;
        return;
    }
    size_t n = strlen(f->json) + strlen(item) + 2;
    char *joined = (char *)malloc(n);
    snprintf(joined, n, "%s,%s", f->json, item);
    free(f->json);
    free(item);
    f->json = joined;
}

void probe_error(probe_ctx *ctx, const char *fmt, ...)
{
    (void)ctx;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_nerrors < (int)(sizeof g_errors / sizeof g_errors[0]))
        g_errors[g_nerrors++] = _strdup(buf);
}

int probe_hr(probe_ctx *ctx, HRESULT hr, const char *what)
{
    if (SUCCEEDED(hr))
        return 0;
    probe_error(ctx, "%s failed: HRESULT 0x%08lx", what, (unsigned long)hr);
    return 1;
}

/* ---- strings ---------------------------------------------------------- */

const char *probe_utf8(const wchar_t *w)
{
    static char bufs[4][1024];
    static int next;
    char *b = bufs[next++ & 3];
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, b, (int)sizeof bufs[0], NULL, NULL))
        b[0] = 0;
    return b;
}

const char *probe_version4(unsigned long long v)
{
    static char bufs[4][32];
    static int next;
    char *b = bufs[next++ & 3];
    snprintf(b, sizeof bufs[0], "%u.%u.%u.%u", (unsigned)(v >> 48) & 0xffff,
             (unsigned)(v >> 32) & 0xffff, (unsigned)(v >> 16) & 0xffff, (unsigned)v & 0xffff);
    return b;
}

const char *probe_vendor_name(unsigned vendor_id)
{
    switch (vendor_id) {
    case 0x1002: return "AMD";
    case 0x1022: return "AMD";
    case 0x10de: return "NVIDIA";
    case 0x8086: return "Intel";
    case 0x1414: return "Microsoft";
    case 0x1af4: return "Red Hat (virtio)";
    case 0x1b36: return "Red Hat (QEMU)";
    case 0x1234: return "QEMU (Bochs VBE)";
    case 0x15ad: return "VMware";
    case 0x10005: return "Mesa";
    case 0x13b5: return "ARM";
    case 0x5143: return "Qualcomm";
    default: return NULL;
    }
}

static int contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!_strnicmp(hay, needle, n))
            return 1;
    return 0;
}

void probe_classify(probe_ctx *ctx, const char *name, long vendor_id, int force_software,
                    const char *force_reason)
{
    static const char *const software_names[] = {
        "Microsoft Basic Render", "Microsoft Basic Display", "WARP", "GDI Generic",
        "llvmpipe", "lavapipe", "softpipe", "SwiftShader",
    };
    const char *reason = NULL;
    char buf[160];
    if (force_software) {
        reason = force_reason;
    } else {
        for (size_t i = 0; i < sizeof software_names / sizeof software_names[0]; i++) {
            if (name && contains_ci(name, software_names[i])) {
                snprintf(buf, sizeof buf, "name contains \"%s\"", software_names[i]);
                reason = buf;
                break;
            }
        }
        if (!reason && vendor_id == 0x1414) {
            reason = "PCI vendor 0x1414 (Microsoft)";
        }
    }
    probe_bool(ctx, PROBE_ADAPTER, "software", reason != NULL);
    if (reason)
        probe_str(ctx, PROBE_ADAPTER, "software_reason", reason);
}

/* ---- window ----------------------------------------------------------- */

static probe_ctx *g_ctx;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        if (g_ctx)
            g_ctx->closed = 1;
        return 0; /* the probe finishes its last frame, then exits */
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && g_ctx)
            g_ctx->closed = 1;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void dpi_aware(void)
{
    typedef BOOL(WINAPI * ctx_fn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    ctx_fn set_ctx = (ctx_fn)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (!set_ctx || !set_ctx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */))
        SetProcessDPIAware();
}

static void usage(const char *probe, unsigned flags)
{
    char text[1024];
    snprintf(text, sizeof text,
             "usage: %s [--seconds N] [--out FILE] [--width W] [--height H]%s\n\n"
             "Renders an animated scene for N seconds (default 30), reads back known\n"
             "pixels and writes a JSON result to FILE (default %s.json in the current\n"
             "directory). Exit 0 pass, 1 API failure, 2 readback mismatch, 3 usage.\n",
             probe, (flags & PROBE_ALLOW_WARP) ? " [--warp]" : "", probe);
    /* GUI subsystem: print to an attached console if there is one. */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        DWORD written;
        WriteConsoleA(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)strlen(text), &written, NULL);
    }
}

int probe_init(probe_ctx *ctx, int argc, wchar_t **argv, const char *probe,
               const char *api, const char *title, unsigned flags)
{
    memset(ctx, 0, sizeof *ctx);
    g_ctx = ctx;
    ctx->probe = probe;
    ctx->api = api;
    ctx->title = title;
#if defined(_WIN64)
    ctx->arch = "x64";
#else
    ctx->arch = "x86";
#endif
    ctx->seconds = 30;
    ctx->render_start = -1;
    ctx->width = 1024;
    ctx->height = 640;
    swprintf(ctx->out_path, MAX_PATH, L"%hs.json", probe);
    QueryPerformanceFrequency(&ctx->freq);
    QueryPerformanceCounter(&ctx->start);

    for (int i = 1; i < argc; i++) {
        const wchar_t *a = argv[i];
        const wchar_t *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!wcscmp(a, L"--seconds") && v) {
            ctx->seconds = _wtof(v);
            i++;
        } else if (!wcscmp(a, L"--out") && v) {
            swprintf(ctx->out_path, MAX_PATH, L"%ls", v);
            i++;
        } else if (!wcscmp(a, L"--width") && v) {
            ctx->width = _wtoi(v);
            i++;
        } else if (!wcscmp(a, L"--height") && v) {
            ctx->height = _wtoi(v);
            i++;
        } else if (!wcscmp(a, L"--warp") && (flags & PROBE_ALLOW_WARP)) {
            ctx->warp = 1;
        } else if (!wcscmp(a, L"--help") || !wcscmp(a, L"-h") || !wcscmp(a, L"/?")) {
            usage(probe, flags);
            return PROBE_EXIT_USAGE;
        } else {
            usage(probe, flags);
            return PROBE_EXIT_USAGE;
        }
    }
    if (ctx->seconds < 0 || ctx->width < 320 || ctx->height < 240) {
        usage(probe, flags);
        return PROBE_EXIT_USAGE;
    }

    /* Markers: red top-left, blue top-right, green bottom-left. */
    probe_marker m[PROBE_MARKERS] = {
        {"red_top_left", 48, 48, {255, 0, 0}},
        {"blue_top_right", ctx->width - 48, 48, {0, 0, 255}},
        {"green_bottom_left", 48, ctx->height - 48, {0, 255, 0}},
    };
    memcpy(ctx->markers, m, sizeof m);

    dpi_aware();
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"vmlab-win3d-probe";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, ctx->width, ctx->height};
    AdjustWindowRect(&r, style, FALSE);
    wchar_t wtitle[160];
    swprintf(wtitle, 160, L"vmlab-win3d probe: %hs (%hs)", api, ctx->arch);
    ctx->hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, wtitle, style, 40, 40,
                                r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!ctx->hwnd) {
        probe_error(ctx, "CreateWindowEx failed: %lu", GetLastError());
        return 0; /* the caller sees the error and finishes */
    }
    ShowWindow(ctx->hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(ctx->hwnd);
    UpdateWindow(ctx->hwnd);
    probe_pump(ctx);
    return 0;
}

int probe_pump(probe_ctx *ctx)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !ctx->closed;
}

double probe_now(const probe_ctx *ctx)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - ctx->start.QuadPart) / (double)ctx->freq.QuadPart;
}

int probe_last_frame(probe_ctx *ctx)
{
    double now = probe_now(ctx);
    if (ctx->render_start < 0)
        ctx->render_start = now;
    return ctx->closed || now - ctx->render_start >= ctx->seconds;
}

void probe_frame_presented(probe_ctx *ctx)
{
    ctx->frames++;
    ctx->end_time = probe_now(ctx);
}

/* ---- scene ------------------------------------------------------------ */

typedef struct scene {
    probe_vertex *v;
    int n, max;
    float w, h;
} scene;

static void tri(scene *s, float x0, float y0, float x1, float y1, float x2, float y2,
                const float c[4])
{
    if (s->n + 3 > s->max)
        return;
    float xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    for (int i = 0; i < 3; i++) {
        probe_vertex *p = &s->v[s->n++];
        /* pixel space (origin top-left) to NDC (y up) */
        p->x = xs[i] / s->w * 2.0f - 1.0f;
        p->y = 1.0f - ys[i] / s->h * 2.0f;
        p->r = c[0];
        p->g = c[1];
        p->b = c[2];
        p->a = c[3];
    }
}

static void rect(scene *s, float x, float y, float w, float h, const float c[4])
{
    tri(s, x, y, x + w, y, x, y + h, c);
    tri(s, x + w, y, x + w, y + h, x, y + h, c);
}

static float text_width(const char *t, float cell)
{
    return (float)strlen(t) * 6.0f * cell - cell;
}

static void text(scene *s, const char *t, float x, float y, float cell, const float c[4])
{
    for (; *t; t++, x += 6.0f * cell) {
        const uint8_t *g = font_glyph(*t);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col))
                    rect(s, x + col * cell, y + row * cell, cell, cell, c);
    }
}

void probe_set_subtitle(probe_ctx *ctx, const char *t)
{
    snprintf(ctx->subtitle, sizeof ctx->subtitle, "%s", t);
}

int probe_build_scene(probe_ctx *ctx, double t, probe_vertex *v, int max)
{
    scene s = {v, 0, max, (float)ctx->width, (float)ctx->height};
    float w = s.w, h = s.h;

    /* Pinwheel: eight coloured blades turning a quarter turn per second. */
    float cx = w * 0.5f, cy = h * 0.58f, rad = h * 0.27f;
    double a0 = t * M_PI * 0.5;
    for (int i = 0; i < 8; i++) {
        double a = a0 + i * (M_PI / 4.0), b = a + M_PI / 4.0 * 0.8;
        float hue = (float)i / 8.0f;
        float c[4] = {0.5f + 0.5f * (float)cos(2 * M_PI * hue),
                      0.5f + 0.5f * (float)cos(2 * M_PI * (hue - 1.0f / 3.0f)),
                      0.5f + 0.5f * (float)cos(2 * M_PI * (hue - 2.0f / 3.0f)), 1.0f};
        tri(&s, cx, cy, cx + rad * (float)cos(a), cy + rad * (float)sin(a),
            cx + rad * (float)cos(b), cy + rad * (float)sin(b), c);
    }
    /* A white square orbiting the other way, so motion is obvious. */
    {
        double a = -t * M_PI;
        float ox = cx + rad * 1.25f * (float)cos(a), oy = cy + rad * 1.25f * (float)sin(a);
        float c[4] = {0.95f, 0.95f, 0.95f, 1.0f};
        rect(&s, ox - 10, oy - 10, 20, 20, c);
    }

    /* The API name, large, across the top. */
    {
        float cell = h / 64.0f;
        float maxw = w - 220.0f;
        if (text_width(ctx->title, cell) > maxw)
            cell = maxw / ((float)strlen(ctx->title) * 6.0f);
        float c[4] = {1.0f, 0.92f, 0.35f, 1.0f};
        text(&s, ctx->title, (w - text_width(ctx->title, cell)) * 0.5f, 24.0f, cell, c);
    }
    /* Architecture and adapter, small, along the bottom. */
    if (ctx->subtitle[0]) {
        float cell = 3.0f;
        float maxw = w - 220.0f;
        if (text_width(ctx->subtitle, cell) > maxw)
            cell = maxw / ((float)strlen(ctx->subtitle) * 6.0f);
        float c[4] = {0.85f, 0.9f, 1.0f, 1.0f};
        text(&s, ctx->subtitle, (w - text_width(ctx->subtitle, cell)) * 0.5f, h - 24.0f - 7 * cell,
             cell, c);
    }

    /* Readback markers last, so nothing draws over them. */
    for (int i = 0; i < PROBE_MARKERS; i++) {
        const probe_marker *m = &ctx->markers[i];
        float c[4] = {m->rgb[0] / 255.0f, m->rgb[1] / 255.0f, m->rgb[2] / 255.0f, 1.0f};
        rect(&s, (float)m->x - 32, (float)m->y - 32, 64, 64, c);
    }
    return s.n;
}

/* ---- readback --------------------------------------------------------- */

void probe_readback(probe_ctx *ctx, int i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i < 0 || i >= PROBE_MARKERS)
        return;
    g_rb_got[i] = 1;
    g_rb[i][0] = r;
    g_rb[i][1] = g;
    g_rb[i][2] = b;
    ctx->readback_done = 1;
}

void probe_readback_image(probe_ctx *ctx, const uint8_t *base, size_t pitch, int bgra,
                          int bottom_up)
{
    for (int i = 0; i < PROBE_MARKERS; i++) {
        const probe_marker *m = &ctx->markers[i];
        int row = bottom_up ? ctx->height - 1 - m->y : m->y;
        const uint8_t *p = base + (size_t)row * pitch + (size_t)m->x * 4;
        if (bgra)
            probe_readback(ctx, i, p[2], p[1], p[0]);
        else
            probe_readback(ctx, i, p[0], p[1], p[2]);
    }
}

/* ---- HLSL ------------------------------------------------------------- */

typedef HRESULT(WINAPI *d3dcompile_fn)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR,
                                       LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

void *probe_compile_hlsl(probe_ctx *ctx, const char *src, const char *entry, const char *target,
                         size_t *size)
{
    static d3dcompile_fn compile;
    if (!compile) {
        HMODULE m = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m)
            compile = (d3dcompile_fn)(void *)GetProcAddress(m, "D3DCompile");
        if (!compile) {
            probe_error(ctx, "cannot load D3DCompile from System32\\d3dcompiler_47.dll: %lu",
                        GetLastError());
            return NULL;
        }
    }
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = compile(src, strlen(src), ctx->probe, NULL, NULL, entry, target,
                         1u << 15 /* D3DCOMPILE_OPTIMIZATION_LEVEL3 */, 0, &code, &errors);
    if (FAILED(hr)) {
        probe_error(ctx, "D3DCompile %s %s failed: 0x%08lx %s", entry, target, (unsigned long)hr,
                    errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "");
        if (errors)
            ID3D10Blob_Release(errors);
        return NULL;
    }
    if (errors)
        ID3D10Blob_Release(errors);
    *size = ID3D10Blob_GetBufferSize(code);
    void *copy = malloc(*size);
    memcpy(copy, ID3D10Blob_GetBufferPointer(code), *size);
    ID3D10Blob_Release(code);
    return copy;
}

/* ---- result ----------------------------------------------------------- */

static void write_section(FILE *f, probe_section s)
{
    int first = 1;
    fputc('{', f);
    for (int i = 0; i < g_nfacts; i++) {
        if (g_facts[i].section != s)
            continue;
        fprintf(f, "%s\n    \"%s\": ", first ? "" : ",", g_facts[i].key);
        if (g_facts[i].is_list)
            fprintf(f, "[%s]", g_facts[i].json);
        else
            fputs(g_facts[i].json, f);
        first = 0;
    }
    fputs(first ? "}" : "\n  }", f);
}

static void os_version(char *buf, size_t n)
{
    typedef LONG(WINAPI * rtl_fn)(OSVERSIONINFOW *);
    OSVERSIONINFOW vi = {0};
    vi.dwOSVersionInfoSize = sizeof vi;
    rtl_fn rtl = (rtl_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (rtl && rtl(&vi) == 0)
        snprintf(buf, n, "%lu.%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    else
        snprintf(buf, n, "unknown");
}

int probe_finish(probe_ctx *ctx)
{
    double elapsed = ctx->end_time > 0 ? ctx->end_time : probe_now(ctx);
    int rb_ok = ctx->readback_done;
    if (!ctx->readback_done && g_nerrors == 0)
        probe_error(ctx, "no frame was read back");
    for (int i = 0; i < PROBE_MARKERS; i++) {
        if (!g_rb_got[i]) {
            rb_ok = 0;
            continue;
        }
        for (int c = 0; c < 3; c++)
            if (abs((int)g_rb[i][c] - (int)ctx->markers[i].rgb[c]) > 2)
                rb_ok = 0;
    }
    int code = g_nerrors ? PROBE_EXIT_API_FAILURE
               : rb_ok   ? PROBE_EXIT_PASS
                         : PROBE_EXIT_READBACK_MISMATCH;
    const char *result = code == PROBE_EXIT_PASS                ? "pass"
                         : code == PROBE_EXIT_READBACK_MISMATCH ? "readback_mismatch"
                                                                : "api_failure";

    FILE *f = _wfopen(ctx->out_path, L"wb");
    if (!f) {
        /* Nowhere to report; the exit code still says it. */
        return code ? code : PROBE_EXIT_API_FAILURE;
    }
    wchar_t exe_w[MAX_PATH];
    GetModuleFileNameW(NULL, exe_w, MAX_PATH);
    char *exe = json_escape(probe_utf8(exe_w));
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    char osv[64];
    os_version(osv, sizeof osv);
    SYSTEMTIME st;
    GetSystemTime(&st);

    fprintf(f, "{\n  \"schema\": %d,\n", PROBE_SCHEMA_VERSION);
    fprintf(f, "  \"probe\": \"%s\",\n  \"api\": \"%s\",\n  \"arch\": \"%s\",\n", ctx->probe,
            ctx->api, ctx->arch);
    fprintf(f, "  \"result\": \"%s\",\n  \"exit_code\": %d,\n", result, code);
    fprintf(f, "  \"exe\": %s,\n  \"pid\": %lu,\n  \"session_id\": %lu,\n", exe,
            GetCurrentProcessId(), session);
    fprintf(f, "  \"os_version\": \"%s\",\n", osv);
    fprintf(f, "  \"finished_utc\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\",\n", st.wYear, st.wMonth,
            st.wDay, st.wHour, st.wMinute, st.wSecond);
    fprintf(f, "  \"args\": {\"seconds\": %g, \"warp\": %s, \"width\": %d, \"height\": %d},\n",
            ctx->seconds, ctx->warp ? "true" : "false", ctx->width, ctx->height);
    fputs("  \"adapter\": ", f);
    write_section(f, PROBE_ADAPTER);
    fputs(",\n  \"device\": ", f);
    write_section(f, PROBE_API);
    fprintf(f, ",\n  \"frames_presented\": %ld,\n  \"elapsed_seconds\": %.3f,\n", ctx->frames,
            elapsed);
    fprintf(f, "  \"avg_fps\": %.2f,\n", elapsed > 0 ? ctx->frames / elapsed : 0.0);
    if (ctx->render_start >= 0)
        fprintf(f, "  \"startup_seconds\": %.3f,\n", ctx->render_start);
    fprintf(f, "  \"window_closed_early\": %s,\n", ctx->closed ? "true" : "false");
    fprintf(f, "  \"readback\": {\n    \"ok\": %s,\n    \"tolerance\": 2,\n    \"pixels\": [",
            rb_ok ? "true" : "false");
    for (int i = 0; i < PROBE_MARKERS; i++) {
        const probe_marker *m = &ctx->markers[i];
        fprintf(f, "%s\n      {\"name\": \"%s\", \"x\": %d, \"y\": %d, \"expected\": [%d, %d, %d], ",
                i ? "," : "", m->name, m->x, m->y, m->rgb[0], m->rgb[1], m->rgb[2]);
        if (g_rb_got[i]) {
            int match = 1;
            for (int c = 0; c < 3; c++)
                if (abs((int)g_rb[i][c] - (int)m->rgb[c]) > 2)
                    match = 0;
            fprintf(f, "\"actual\": [%d, %d, %d], \"match\": %s}", g_rb[i][0], g_rb[i][1],
                    g_rb[i][2], match ? "true" : "false");
        } else {
            fputs("\"actual\": null, \"match\": false}", f);
        }
    }
    fputs("\n    ]\n  },\n  \"errors\": [", f);
    for (int i = 0; i < g_nerrors; i++) {
        char *e = json_escape(g_errors[i]);
        fprintf(f, "%s\n    %s", i ? "," : "", e);
        free(e);
    }
    fputs(g_nerrors ? "\n  ]\n}\n" : "]\n}\n", f);
    fclose(f);
    free(exe);
    return code;
}
