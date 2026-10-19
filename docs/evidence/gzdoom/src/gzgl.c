/* gzgl: GZDoom 4.14.2's WGL start-up (src/common/platform/win32/
 * win32glvideo.cpp), one call at a time, with every result logged.
 *
 *  1. Win32GLVideo::SetPixelFormat: a hidden 64x64 CS_OWNDC dummy window,
 *     ChoosePixelFormat with a 32-bit colour, 16-bit depth, no stencil PFD,
 *     SetPixelFormat, wglCreateContext, wglMakeCurrent, then
 *     wglGetProcAddress("wglChoosePixelFormatARB") and
 *     ("wglCreateContextAttribsARB"). GZDoom stops with "Unable to create an
 *     OpenGL render context. Insufficient driver support for context
 *     creation" when the first of those is NULL.
 *  2. SetupPixelFormat: wglChoosePixelFormatARB on the real window with
 *     GZDoom's attribute list, then SetPixelFormat.
 *  3. InitHardware: wglCreateContextAttribsARB for 4.6 down to 3.3, core then
 *     compatibility profile, then wglCreateContext.
 *  4. A cleared frame, read back, and SwapBuffers.
 *
 *   gzgl.exe [--log FILE] [--frames N] [--list]
 *
 * --list also logs every pixel format of the dummy window's DC.
 *
 * Exit code 0 when every step works and the read-back pixel is the clear
 * colour; otherwise the number of the step that failed. */
#include <windows.h>
#include <GL/gl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_ACCELERATION_ARB 0x2003
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_RED_BITS_ARB 0x2015
#define WGL_GREEN_BITS_ARB 0x2017
#define WGL_BLUE_BITS_ARB 0x2019
#define WGL_ALPHA_BITS_ARB 0x201B
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 1
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 2

/* Like GZDoom (src/common/rendering/gl_load/gl_load.c), opengl32.dll is not
 * imported: it is loaded with LoadLibraryA("OpenGL32.DLL") on the first wgl
 * call, after GDI's ChoosePixelFormat and SetPixelFormat have run. */
static HMODULE gl_lib;
static FARPROC gl_fn(const char *name)
{
    if (!gl_lib)
        gl_lib = LoadLibraryA("OpenGL32.DLL");
    return gl_lib ? GetProcAddress(gl_lib, name) : NULL;
}
#define GL_FN(ret, name, params, args) \
    static ret dyn_##name params { return ((ret(WINAPI *) params)(void *)gl_fn(#name))args; }
GL_FN(HGLRC, wglCreateContext, (HDC a), (a))
GL_FN(BOOL, wglMakeCurrent, (HDC a, HGLRC b), (a, b))
GL_FN(BOOL, wglDeleteContext, (HGLRC a), (a))
GL_FN(PROC, wglGetProcAddress, (LPCSTR a), (a))
GL_FN(const GLubyte *, glGetString, (GLenum a), (a))
GL_FN(GLenum, glGetError, (void), ())
static void dyn_glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    ((void(WINAPI *)(GLint, GLint, GLsizei, GLsizei))(void *)gl_fn("glViewport"))(x, y, w, h);
}
static void dyn_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    ((void(WINAPI *)(GLfloat, GLfloat, GLfloat, GLfloat))(void *)gl_fn("glClearColor"))(r, g, b, a);
}
static void dyn_glClear(GLbitfield m) { ((void(WINAPI *)(GLbitfield))(void *)gl_fn("glClear"))(m); }
static void dyn_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p)
{
    ((void(WINAPI *)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))(void *)gl_fn(
        "glReadPixels"))(x, y, w, h, f, t, p);
}
#define wglCreateContext dyn_wglCreateContext
#define wglMakeCurrent dyn_wglMakeCurrent
#define wglDeleteContext dyn_wglDeleteContext
#define wglGetProcAddress dyn_wglGetProcAddress
#define glGetString dyn_glGetString
#define glGetError dyn_glGetError
#define glViewport dyn_glViewport
#define glClearColor dyn_glClearColor
#define glClear dyn_glClear
#define glReadPixels dyn_glReadPixels

typedef BOOL(WINAPI *choose_fn)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
typedef HGLRC(WINAPI *create_attribs_fn)(HDC, HGLRC, const int *);
typedef const char *(WINAPI *ext_fn)(HDC);

static FILE *logf;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    if (logf) {
        va_start(ap, fmt);
        vfprintf(logf, fmt, ap);
        va_end(ap);
        fprintf(logf, "\n");
        fflush(logf);
    }
}

/* Which of opengl32.dll and the ICD are loaded right now. */
static void modules(const char *when)
{
    say("modules %s: opengl32.dll %p, viogpu_wgl.dll %p", when, (void *)GetModuleHandleA("opengl32.dll"),
        (void *)GetModuleHandleA("viogpu_wgl.dll"));
}

static void describe(HDC dc, int pf, const char *what)
{
    PIXELFORMATDESCRIPTOR p;
    memset(&p, 0, sizeof p);
    if (!DescribePixelFormat(dc, pf, sizeof p, &p)) {
        say("%s: pf %d DescribePixelFormat failed, error %lu", what, pf, GetLastError());
        return;
    }
    say("%s: pf %d flags 0x%08lx%s%s%s colour %u (r%u g%u b%u a%u) depth %u stencil %u accum %u",
        what, pf, p.dwFlags, (p.dwFlags & PFD_GENERIC_FORMAT) ? " GENERIC" : "",
        (p.dwFlags & PFD_GENERIC_ACCELERATED) ? " GENERIC_ACCELERATED" : "",
        (p.dwFlags & PFD_DOUBLEBUFFER) ? " DOUBLEBUFFER" : "", p.cColorBits, p.cRedBits,
        p.cGreenBits, p.cBlueBits, p.cAlphaBits, p.cDepthBits, p.cStencilBits, p.cAccumBits);
}

/* Win32GLVideo::InitDummy. */
static HWND init_dummy(void)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    RECT r = {0, 0, 64, 64};
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"GZDoomOpenGLDummyWindow";
    if (!RegisterClassW(&wc))
        return NULL;
    DWORD exstyle = WS_EX_CLIENTEDGE, style = WS_SYSMENU | WS_BORDER | WS_CAPTION;
    AdjustWindowRectEx(&r, style, FALSE, exstyle);
    HWND w = CreateWindowExW(exstyle, L"GZDoomOpenGLDummyWindow", L"gzgl",
                             WS_CLIPSIBLINGS | WS_CLIPCHILDREN | style, 0, 0, r.right - r.left,
                             r.bottom - r.top, NULL, NULL, inst, NULL);
    if (w)
        ShowWindow(w, SW_HIDE);
    return w;
}

static LRESULT CALLBACK main_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

int main(int argc, char **argv)
{
    int frames = 120, list = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--log") && i + 1 < argc)
            logf = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--list"))
            list = 1;
    }

    /* Step 1: Win32GLVideo::SetPixelFormat. */
    say("step 1: dummy window, legacy context, extension entry points");
    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0};
    HWND dummy = init_dummy();
    say("dummy window %p", (void *)dummy);
    HDC ddc = GetDC(dummy);
    modules("at start");
    int npf = DescribePixelFormat(ddc, 1, sizeof(PIXELFORMATDESCRIPTOR), NULL);
    say("pixel formats on the dummy DC: %d", npf);
    for (int i = 1; list && i <= npf; i++)
        describe(ddc, i, "format");
    int pf = ChoosePixelFormat(ddc, &pfd);
    say("ChoosePixelFormat -> %d (error %lu)", pf, pf ? 0 : GetLastError());
    modules("after ChoosePixelFormat");
    describe(ddc, pf, "dummy chosen");
    DescribePixelFormat(ddc, pf, sizeof pfd, &pfd);
    BOOL ok = SetPixelFormat(ddc, pf, &pfd);
    say("SetPixelFormat -> %d (error %lu)", ok, ok ? 0 : GetLastError());
    modules("after SetPixelFormat");
    HGLRC drc = wglCreateContext(ddc);
    say("wglCreateContext -> %p (error %lu)", (void *)drc, drc ? 0 : GetLastError());
    ok = wglMakeCurrent(ddc, drc);
    say("wglMakeCurrent -> %d (error %lu)", ok, ok ? 0 : GetLastError());
    if (drc && ok)
        say("GL_RENDERER %s | GL_VERSION %s", (const char *)glGetString(GL_RENDERER),
            (const char *)glGetString(GL_VERSION));
    if (!drc) {
        /* Diagnostics only; GZDoom does none of this. */
        say("diag: GetPixelFormat now %d", GetPixelFormat(ddc));
        int (WINAPI *wgpf)(HDC) = (int(WINAPI *)(HDC))(void *)gl_fn("wglGetPixelFormat");
        say("diag: opengl32 wglGetPixelFormat %d", wgpf ? wgpf(ddc) : -1);
        ok = SetPixelFormat(ddc, pf, &pfd);
        say("diag: SetPixelFormat again -> %d (error %lu)", ok, ok ? 0 : GetLastError());
        HGLRC t = wglCreateContext(ddc);
        say("diag: wglCreateContext again -> %p (error %lu)", (void *)t, t ? 0 : GetLastError());
        if (t)
            wglDeleteContext(t);
    }
    choose_fn choose = (choose_fn)(void *)wglGetProcAddress("wglChoosePixelFormatARB");
    create_attribs_fn create = (create_attribs_fn)(void *)wglGetProcAddress("wglCreateContextAttribsARB");
    ext_fn ext = (ext_fn)(void *)wglGetProcAddress("wglGetExtensionsStringARB");
    say("wglGetProcAddress: wglChoosePixelFormatARB %p, wglCreateContextAttribsARB %p, "
        "wglGetExtensionsStringARB %p",
        (void *)choose, (void *)create, (void *)ext);
    if (ext)
        say("WGL extensions: %s", ext(ddc));
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(drc);
    ReleaseDC(dummy, ddc);
    DestroyWindow(dummy);
    UnregisterClassW(L"GZDoomOpenGLDummyWindow", GetModuleHandleW(NULL));
    if (!choose) {
        say("FAIL step 1: GZDoom would stop here with \"Insufficient driver support for context creation\"");
        return 1;
    }

    /* Step 2: SetupPixelFormat on the real window. */
    say("step 2: wglChoosePixelFormatARB on the game window");
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = main_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"gzgl";
    RegisterClassW(&wc);
    HWND win = CreateWindowExW(0, L"gzgl", L"gzgl", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 640,
                               400, NULL, NULL, wc.hInstance, NULL);
    HDC dc = GetDC(win);
    int attribs[] = {WGL_DEPTH_BITS_ARB, 24, WGL_STENCIL_BITS_ARB, 8, WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
                     WGL_SUPPORT_OPENGL_ARB, GL_TRUE, WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
                     WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB, WGL_RED_BITS_ARB, 8,
                     WGL_GREEN_BITS_ARB, 8, WGL_BLUE_BITS_ARB, 8, WGL_ALPHA_BITS_ARB, 8, 0, 0};
    float fattribs[] = {0.0f, 0.0f};
    int gpf = 0;
    UINT nformats = 0;
    ok = choose(dc, attribs, fattribs, 1, &gpf, &nformats);
    say("wglChoosePixelFormatARB -> %d, %u formats, first %d", ok, nformats, gpf);
    if (!ok || !nformats) {
        say("FAIL step 2");
        return 2;
    }
    describe(dc, gpf, "game chosen");
    ok = SetPixelFormat(dc, gpf, NULL);
    say("SetPixelFormat(NULL pfd) -> %d (error %lu)", ok, ok ? 0 : GetLastError());
    if (!ok) {
        say("FAIL step 2");
        return 2;
    }

    /* Step 3: InitHardware. */
    say("step 3: context creation");
    static const int versions[] = {46, 45, 44, 43, 42, 41, 40, 33, -1};
    HGLRC rc = NULL;
    for (int prof = WGL_CONTEXT_CORE_PROFILE_BIT_ARB; !rc && prof <= WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
         prof++) {
        for (int i = 0; !rc && versions[i] > 0; i++) {
            int ca[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, versions[i] / 10, WGL_CONTEXT_MINOR_VERSION_ARB,
                        versions[i] % 10, WGL_CONTEXT_FLAGS_ARB, 0, WGL_CONTEXT_PROFILE_MASK_ARB, prof, 0};
            rc = create(dc, 0, ca);
            say("wglCreateContextAttribsARB %d.%d %s -> %p (error %lu)", versions[i] / 10,
                versions[i] % 10, prof == 1 ? "core" : "compat", (void *)rc, rc ? 0 : GetLastError());
        }
        if (!rc && prof == WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB) {
            rc = wglCreateContext(dc);
            say("wglCreateContext -> %p", (void *)rc);
        }
    }
    if (!rc || !wglMakeCurrent(dc, rc)) {
        say("FAIL step 3");
        return 3;
    }
    say("GL_RENDERER %s | GL_VERSION %s", (const char *)glGetString(GL_RENDERER),
        (const char *)glGetString(GL_VERSION));

    /* Step 4: draw. */
    say("step 4: %d cleared frames", frames);
    unsigned char px[4] = {0};
    for (int f = 0; f < frames; f++) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT)
                frames = f;
            DispatchMessageW(&m);
        }
        float t = (float)(f % 60) / 60.0f;
        glViewport(0, 0, 640, 400);
        glClearColor(f == frames - 1 ? 0.0f : t, 0.5f, f == frames - 1 ? 1.0f : 1.0f - t, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (f == frames - 1)
            glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        SwapBuffers(dc);
        Sleep(16);
    }
    GLenum err = glGetError();
    say("read back %u %u %u %u, glGetError 0x%x", px[0], px[1], px[2], px[3], err);
    int good = px[0] < 4 && px[1] > 120 && px[1] < 135 && px[2] > 250 && err == GL_NO_ERROR;
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    say(good ? "PASS" : "FAIL step 4");
    return good ? 0 : 4;
}
