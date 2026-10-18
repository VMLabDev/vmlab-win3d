/* Host GL fence latency on a render node, the way virglrenderer's vrend
 * (QEMU's ctx 0 fences) uses it: glFenceSync + glFlush on one context, a
 * sync thread with a shared context in glClientWaitSync. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gbm.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static EGLDisplay dpy; static EGLContext ctx2;
static PFNGLFENCESYNCPROC FenceSync; static PFNGLCLIENTWAITSYNCPROC ClientWaitSync; static PFNGLDELETESYNCPROC DeleteSync;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static volatile GLsync cur; static volatile double t_signal; static volatile int done;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static void *thr(void *a) {
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx2);
   for (;;) {
      pthread_mutex_lock(&mu);
      while (!cur && !done) pthread_cond_wait(&cv, &mu);
      GLsync s = cur; pthread_mutex_unlock(&mu);
      if (!s) break;
      ClientWaitSync(s, 0, 1000000000);
      pthread_mutex_lock(&mu); t_signal = now(); cur = 0; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
   }
   return NULL;
}
int main(int argc, char **argv) {
   int fd = open(argv[1], O_RDWR);
   struct gbm_device *gbm = gbm_create_device(fd);
   dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
   eglInitialize(dpy, NULL, NULL);
   printf("vendor: %s\n", eglQueryString(dpy, EGL_VENDOR));
   eglBindAPI(EGL_OPENGL_API);
   EGLint at[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, at);
   ctx2 = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, ctx, at);
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
   printf("renderer: %s\n", glGetString(GL_RENDERER));
   FenceSync = (void *)eglGetProcAddress("glFenceSync"); ClientWaitSync = (void *)eglGetProcAddress("glClientWaitSync"); DeleteSync = (void *)eglGetProcAddress("glDeleteSync");
   pthread_t t; pthread_create(&t, NULL, thr, NULL);
   int n = 300; double v[300], sum = 0, mx = 0;
   for (int i = 0; i < n; i++) {
      usleep(3000);
      double t0 = now();
      GLsync s = FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      glFlush();
      pthread_mutex_lock(&mu); cur = s; pthread_cond_broadcast(&cv);
      while (cur) pthread_cond_wait(&cv, &mu);
      pthread_mutex_unlock(&mu);
      v[i] = t_signal - t0; sum += v[i]; if (v[i] > mx) mx = v[i];
      DeleteSync(s);
   }
   int over1 = 0; for (int i = 0; i < n; i++) over1 += v[i] > 1.0;
   printf("fence latency ms: mean=%.3f max=%.3f over1ms=%d/%d\n", sum / n, mx, over1, n);
   pthread_mutex_lock(&mu); done = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
   return 0;
}
