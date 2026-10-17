/* OpenGL probe: a WGL compatibility context through the system opengl32.dll,
 * which loads the display driver's ICD (OpenGLDriverName) or falls back to
 * Microsoft's GDI generic renderer. Draws with GL 1.1 vertex arrays so it runs
 * on both, reads back with glReadPixels, and reports which ICD DLL loaded. */
#define PSAPI_VERSION 2
#include "../common/probe.h"

#include <GL/gl.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif

typedef const char *(WINAPI *wglGetExtensionsStringARB_fn)(HDC);
typedef BOOL(WINAPI *wglSwapIntervalEXT_fn)(int);

static const char *gl_string(GLenum e)
{
    const GLubyte *s = glGetString(e);
    return s ? (const char *)s : "";
}

static int has_ext(const char *list, const char *ext)
{
    size_t n = strlen(ext);
    for (const char *p = list; p && (p = strstr(p, ext)) != NULL; p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0))
            return 1;
    return 0;
}

/* Finds loaded modules exporting the ICD entry point DrvSetPixelFormat. */
static void report_icd(probe_ctx *ctx)
{
    HMODULE mods[512];
    DWORD needed = 0;
    int found = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &needed))
        return;
    for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 512; i++) {
        if (!GetProcAddress(mods[i], "DrvSetPixelFormat"))
            continue;
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(mods[i], path, MAX_PATH);
        probe_list_add(ctx, PROBE_API, "icd_modules", probe_utf8(path));
        found++;
    }
    probe_bool(ctx, PROBE_API, "icd_loaded", found > 0);
}

static int run(probe_ctx *ctx)
{
    HDC dc = GetDC(ctx->hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf) {
        probe_error(ctx, "ChoosePixelFormat failed: %lu", GetLastError());
        return 1;
    }
    DescribePixelFormat(dc, pf, sizeof pfd, &pfd);
    probe_int(ctx, PROBE_API, "pixel_format", pf);
    int generic = (pfd.dwFlags & PFD_GENERIC_FORMAT) && !(pfd.dwFlags & PFD_GENERIC_ACCELERATED);
    probe_bool(ctx, PROBE_API, "pfd_generic_format", (pfd.dwFlags & PFD_GENERIC_FORMAT) != 0);
    probe_bool(ctx, PROBE_API, "pfd_generic_accelerated", (pfd.dwFlags & PFD_GENERIC_ACCELERATED) != 0);
    if (!SetPixelFormat(dc, pf, &pfd)) {
        probe_error(ctx, "SetPixelFormat failed: %lu", GetLastError());
        return 1;
    }
    HGLRC rc = wglCreateContext(dc);
    if (!rc) {
        probe_error(ctx, "wglCreateContext failed: %lu", GetLastError());
        return 1;
    }
    if (!wglMakeCurrent(dc, rc)) {
        probe_error(ctx, "wglMakeCurrent failed: %lu", GetLastError());
        return 1;
    }
    probe_str(ctx, PROBE_API, "context", "wglCreateContext (compatibility)");

    const char *renderer = gl_string(GL_RENDERER);
    const char *vendor = gl_string(GL_VENDOR);
    probe_str(ctx, PROBE_ADAPTER, "name", renderer);
    probe_str(ctx, PROBE_ADAPTER, "vendor", vendor);
    probe_str(ctx, PROBE_API, "gl_renderer", renderer);
    probe_str(ctx, PROBE_API, "gl_vendor", vendor);
    probe_str(ctx, PROBE_API, "gl_version", gl_string(GL_VERSION));
    glGetError();
    const char *glsl = gl_string(GL_SHADING_LANGUAGE_VERSION);
    glGetError(); /* GL 1.1 reports GL_INVALID_ENUM for the GLSL string */
    probe_str(ctx, PROBE_API, "gl_shading_language_version", glsl);
    probe_classify(ctx, renderer, -1, generic, "pixel format is PFD_GENERIC_FORMAT (GDI software)");
    report_icd(ctx);

    char sub[160];
    snprintf(sub, sizeof sub, "%s - %s", ctx->arch, renderer);
    probe_set_subtitle(ctx, sub);

    wglGetExtensionsStringARB_fn get_ext =
        (wglGetExtensionsStringARB_fn)(void *)wglGetProcAddress("wglGetExtensionsStringARB");
    const char *wgl_ext = get_ext ? get_ext(dc) : "";
    probe_bool(ctx, PROBE_API, "wgl_arb_create_context", has_ext(wgl_ext, "WGL_ARB_create_context"));
    wglSwapIntervalEXT_fn swap_interval =
        (wglSwapIntervalEXT_fn)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    probe_bool(ctx, PROBE_API, "swap_interval_1", swap_interval && swap_interval(1));

    probe_vertex *verts = (probe_vertex *)malloc(PROBE_MAX_VERTICES * sizeof(probe_vertex));
    uint8_t *pixels = (uint8_t *)malloc((size_t)ctx->width * ctx->height * 4);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(probe_vertex), &verts[0].x);
    glColorPointer(4, GL_FLOAT, sizeof(probe_vertex), &verts[0].r);
    for (;;) {
        probe_pump(ctx);
        int last = probe_last_frame(ctx);
        int n = probe_build_scene(ctx, probe_now(ctx), verts, PROBE_MAX_VERTICES);
        glViewport(0, 0, ctx->width, ctx->height);
        glClearColor(probe_clear_color[0], probe_clear_color[1], probe_clear_color[2],
                     probe_clear_color[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, n);
        if (last) {
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(0, 0, ctx->width, ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            probe_error(ctx, "glGetError 0x%04x after frame %ld", err, ctx->frames);
            return 1;
        }
        if (last)
            probe_readback_image(ctx, pixels, (size_t)ctx->width * 4, 0, 1);
        if (!SwapBuffers(dc)) {
            probe_error(ctx, "SwapBuffers failed: %lu", GetLastError());
            return 1;
        }
        probe_frame_presented(ctx);
        if (last)
            break;
    }
    free(pixels);
    free(verts);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(ctx->hwnd, dc);
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    probe_ctx ctx;
    int rc = probe_init(&ctx, argc, argv, "opengl", "OpenGL", "OPENGL", 0);
    if (rc)
        return rc;
    if (ctx.hwnd)
        run(&ctx);
    return probe_finish(&ctx);
}
