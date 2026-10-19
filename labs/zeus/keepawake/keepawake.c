// SPDX-License-Identifier: MIT
// Keep one GPU out of runtime D3 (runtime power management) while it runs,
// without root. docs/zeus.md, "RTX 4060: runtime power management".
//
//   keepawake [--pci 0000:01:00.0] [--interval-ms 100] [--hold-only]
//
// Opens the GPU at the PCI address through EGL's device platform
// (EGL_EXT_device_enumeration, EGL_EXT_platform_device), makes an OpenGL ES
// context current without a surface, and then, every interval, clears a
// 16x16 renderbuffer and waits for it (glFinish). Under NVIDIA's
// fine-grained runtime D3 (DynamicPowerManagement=2) an open context alone
// does not keep the GPU awake: the driver suspends it once it is idle.
// Small regular work does. --hold-only keeps the context and submits
// nothing, for comparison. It runs until SIGINT or SIGTERM and prints the
// PCI device's runtime_status once a minute.
//
// Build: cc -O2 -o keepawake keepawake.c -lEGL   (labs/zeus/keepawake/build.sh)
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// The few GLES entry points used, through eglGetProcAddress (no GL headers).
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned int GLbitfield;
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_RGBA8 0x8058
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RENDERER 0x1F01

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

static void die(const char *msg) {
    fprintf(stderr, "keepawake: %s (EGL error 0x%x)\n", msg, eglGetError());
    exit(1);
}

static void runtime_status(const char *pci, char *buf, size_t n) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/power/runtime_status", pci);
    FILE *f = fopen(path, "r");
    if (!f || !fgets(buf, (int)n, f)) snprintf(buf, n, "unknown\n");
    if (f) fclose(f);
    buf[strcspn(buf, "\n")] = 0;
}

// The render node (/dev/dri/renderDNNN) of the PCI device, from sysfs.
static int render_node(const char *pci, char *out, size_t n) {
    char path[PATH_MAX];
    for (int i = 128; i < 192; i++) {
        snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/drm/renderD%d", pci, i);
        if (access(path, F_OK) == 0) {
            snprintf(out, n, "/dev/dri/renderD%d", i);
            return 0;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    const char *pci = "0000:01:00.0";
    long interval_ms = 100;
    int hold_only = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pci") && i + 1 < argc) pci = argv[++i];
        else if (!strcmp(argv[i], "--interval-ms") && i + 1 < argc) interval_ms = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--hold-only")) hold_only = 1;
        else {
            fprintf(stderr, "usage: keepawake [--pci 0000:01:00.0] [--interval-ms 100] [--hold-only]\n");
            return 2;
        }
    }
    if (interval_ms < 1) interval_ms = 1;

    char node[64];
    if (render_node(pci, node, sizeof node)) {
        fprintf(stderr, "keepawake: no render node for PCI device %s\n", pci);
        return 1;
    }

    PFNEGLQUERYDEVICESEXTPROC queryDevices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC queryDeviceString = (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!queryDevices || !queryDeviceString || !getPlatformDisplay) die("EGL device enumeration is not available");

    EGLDeviceEXT devs[16];
    EGLint ndev = 0;
    if (!queryDevices(16, devs, &ndev)) die("eglQueryDevicesEXT failed");
    EGLDeviceEXT dev = EGL_NO_DEVICE_EXT;
    for (EGLint i = 0; i < ndev && dev == EGL_NO_DEVICE_EXT; i++) {
        const char *rn = queryDeviceString(devs[i], EGL_DRM_RENDER_NODE_FILE_EXT);
        if (rn && !strcmp(rn, node)) dev = devs[i];
    }
    if (dev == EGL_NO_DEVICE_EXT) {
        fprintf(stderr, "keepawake: no EGL device has render node %s (%s)\n", node, pci);
        return 1;
    }

    EGLDisplay dpy = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, dev, NULL);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) die("eglInitialize failed");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) die("eglBindAPI failed");
    static const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) die("eglCreateContext failed");
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) die("eglMakeCurrent failed");

    const unsigned char *(*GetString)(GLenum) = (void *)eglGetProcAddress("glGetString");
    void (*GenFramebuffers)(GLsizei, GLuint *) = (void *)eglGetProcAddress("glGenFramebuffers");
    void (*BindFramebuffer)(GLenum, GLuint) = (void *)eglGetProcAddress("glBindFramebuffer");
    void (*GenRenderbuffers)(GLsizei, GLuint *) = (void *)eglGetProcAddress("glGenRenderbuffers");
    void (*BindRenderbuffer)(GLenum, GLuint) = (void *)eglGetProcAddress("glBindRenderbuffer");
    void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = (void *)eglGetProcAddress("glRenderbufferStorage");
    void (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = (void *)eglGetProcAddress("glFramebufferRenderbuffer");
    void (*Viewport)(int, int, GLsizei, GLsizei) = (void *)eglGetProcAddress("glViewport");
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = (void *)eglGetProcAddress("glClearColor");
    void (*Clear)(GLbitfield) = (void *)eglGetProcAddress("glClear");
    void (*Finish)(void) = (void *)eglGetProcAddress("glFinish");
    if (!GetString || !GenFramebuffers || !BindFramebuffer || !GenRenderbuffers || !BindRenderbuffer ||
        !RenderbufferStorage || !FramebufferRenderbuffer || !Viewport || !ClearColor || !Clear || !Finish)
        die("a GLES entry point is missing");

    GLuint fb, rb;
    GenFramebuffers(1, &fb);
    BindFramebuffer(GL_FRAMEBUFFER, fb);
    GenRenderbuffers(1, &rb);
    BindRenderbuffer(GL_RENDERBUFFER, rb);
    RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
    FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    Viewport(0, 0, 16, 16);

    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    char st[64];
    runtime_status(pci, st, sizeof st);
    printf("keepawake: pid %d, %s (%s), renderer \"%s\", %s, runtime_status %s\n", (int)getpid(), pci, node,
           (const char *)GetString(GL_RENDERER),
           hold_only ? "holding a context only" : "clearing every interval", st);
    if (!hold_only) printf("keepawake: interval %ld ms\n", interval_ms);
    fflush(stdout);

    struct timespec ts = {interval_ms / 1000, (interval_ms % 1000) * 1000000L};
    time_t last = time(NULL);
    unsigned long n = 0;
    while (!stop) {
        if (!hold_only) {
            ClearColor((float)(n & 1), 0.f, 0.f, 1.f);
            Clear(GL_COLOR_BUFFER_BIT);
            Finish();
            n++;
        }
        nanosleep(&ts, NULL);
        if (time(NULL) - last >= 60) {
            last = time(NULL);
            runtime_status(pci, st, sizeof st);
            printf("keepawake: %ld runtime_status %s, %lu clears\n", (long)last, st, n);
            fflush(stdout);
        }
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    printf("keepawake: stopped after %lu clears\n", n);
    return 0;
}
