/* gltrace: an app-local opengl32.dll that logs the WGL calls an application
 * makes and forwards everything to oglreal.dll, a renamed copy of the
 * guest's own System32\opengl32.dll placed next to it. GDI's
 * ChoosePixelFormat/SetPixelFormat/DescribePixelFormat reach the wgl*
 * pixel-format entry points through the loaded "opengl32.dll", so they are
 * logged too. Every other export is a PE forwarder (gltrace.def, generated
 * by build.sh). Log: gltrace.txt in the application's directory. */
#include <windows.h>
#include <stdio.h>

static HMODULE real;
static FILE *logf;
static CRITICAL_SECTION lock;

static void say(const char *fmt, ...)
{
    if (!logf)
        return;
    EnterCriticalSection(&lock);
    va_list ap;
    va_start(ap, fmt);
    fprintf(logf, "[%5lu] ", GetCurrentThreadId());
    vfprintf(logf, fmt, ap);
    fprintf(logf, "\n");
    fflush(logf);
    va_end(ap);
    LeaveCriticalSection(&lock);
}

static FARPROC fn(const char *name)
{
    if (!real) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t *s = wcsrchr(path, L'\\');
        wcscpy(s + 1, L"oglreal.dll");
        real = LoadLibraryW(path);
    }
    return real ? GetProcAddress(real, name) : NULL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&lock);
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(inst, path, MAX_PATH);
        wchar_t *s = wcsrchr(path, L'\\');
        wcscpy(s + 1, L"gltrace.txt");
        logf = _wfopen(path, L"w");
        say("attach");
    }
    return TRUE;
}

static void pfd_say(const char *what, int pf, const PIXELFORMATDESCRIPTOR *p)
{
    if (!p) {
        say("%s pf %d (no pfd)", what, pf);
        return;
    }
    say("%s pf %d flags 0x%08lx colour %u (r%u g%u b%u a%u) depth %u stencil %u accum %u", what,
        pf, p->dwFlags, p->cColorBits, p->cRedBits, p->cGreenBits, p->cBlueBits, p->cAlphaBits,
        p->cDepthBits, p->cStencilBits, p->cAccumBits);
}

int WINAPI wglChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *p)
{
    int r = ((int(WINAPI *)(HDC, const PIXELFORMATDESCRIPTOR *))fn("wglChoosePixelFormat"))(dc, p);
    pfd_say("wglChoosePixelFormat request", 0, p);
    say("wglChoosePixelFormat(dc %p) -> %d (error %lu)", (void *)dc, r, GetLastError());
    return r;
}

static int describe_calls;

int WINAPI wglDescribePixelFormat(HDC dc, int pf, UINT n, PIXELFORMATDESCRIPTOR *p)
{
    int r = ((int(WINAPI *)(HDC, int, UINT, PIXELFORMATDESCRIPTOR *))fn("wglDescribePixelFormat"))(
        dc, pf, n, p);
    if (describe_calls++ < 8 || !p)
        say("wglDescribePixelFormat(dc %p, %d, %u, %p) -> %d", (void *)dc, pf, n, (void *)p, r);
    if (p && r && describe_calls < 8)
        pfd_say("  described", pf, p);
    return r;
}

BOOL WINAPI wglSetPixelFormat(HDC dc, int pf, const PIXELFORMATDESCRIPTOR *p)
{
    BOOL r = ((BOOL(WINAPI *)(HDC, int, const PIXELFORMATDESCRIPTOR *))fn("wglSetPixelFormat"))(dc, pf, p);
    say("wglSetPixelFormat(dc %p, %d) -> %d (error %lu)", (void *)dc, pf, r, r ? 0 : GetLastError());
    return r;
}

int WINAPI wglGetPixelFormat(HDC dc)
{
    int r = ((int(WINAPI *)(HDC))fn("wglGetPixelFormat"))(dc);
    say("wglGetPixelFormat(dc %p) -> %d", (void *)dc, r);
    return r;
}

HGLRC WINAPI wglCreateContext(HDC dc)
{
    HGLRC r = ((HGLRC(WINAPI *)(HDC))fn("wglCreateContext"))(dc);
    say("wglCreateContext(dc %p, window %p) -> %p (error %lu)", (void *)dc, (void *)WindowFromDC(dc),
        (void *)r, r ? 0 : GetLastError());
    return r;
}

BOOL WINAPI wglMakeCurrent(HDC dc, HGLRC rc)
{
    BOOL r = ((BOOL(WINAPI *)(HDC, HGLRC))fn("wglMakeCurrent"))(dc, rc);
    say("wglMakeCurrent(dc %p, rc %p) -> %d (error %lu)", (void *)dc, (void *)rc, r, r ? 0 : GetLastError());
    if (r && rc) {
        const char *(WINAPI * gs)(unsigned) = (const char *(WINAPI *)(unsigned))fn("glGetString");
        const char *v = gs(0x1F00), *rn = gs(0x1F01);
        say("  GL_VENDOR %s, GL_RENDERER %s", v ? v : "(null)", rn ? rn : "(null)");
    }
    return r;
}

BOOL WINAPI wglDeleteContext(HGLRC rc)
{
    BOOL r = ((BOOL(WINAPI *)(HGLRC))fn("wglDeleteContext"))(rc);
    say("wglDeleteContext(%p) -> %d", (void *)rc, r);
    return r;
}

PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    PROC r = ((PROC(WINAPI *)(LPCSTR))fn("wglGetProcAddress"))(name);
    if (!strncmp(name, "wgl", 3) || !r)
        say("wglGetProcAddress(%s) -> %p (current rc %p)", name, (void *)r,
            (void *)((HGLRC(WINAPI *)(void))fn("wglGetCurrentContext"))());
    return r;
}
