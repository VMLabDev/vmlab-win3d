/* LD_PRELOAD shim for QEMU: logs virglrenderer fence creation, fence
 * callbacks and virgl_renderer_poll calls with CLOCK_MONOTONIC ms, to the
 * file in $N5_SHIM_LOG. Diagnostic only. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <virglrenderer.h>
static FILE *lf; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
#define LOG(...) do { pthread_mutex_lock(&mu); if (!lf) lf = fopen(getenv("N5_SHIM_LOG"), "w"); fprintf(lf, "%.3f ", now()); fprintf(lf, __VA_ARGS__); fflush(lf); pthread_mutex_unlock(&mu); } while (0)
static struct virgl_renderer_callbacks real, mine;
static void w_fence(void *c, uint32_t f) { LOG("cb_fence %u\n", f); real.write_fence(c, f); }
static void w_ctx_fence(void *c, uint32_t ctx, uint32_t ring, uint64_t f) { LOG("cb_ctx_fence ctx=%u ring=%u %llu\n", ctx, ring, (unsigned long long)f); real.write_context_fence(c, ctx, ring, f); }
int virgl_renderer_init(void *cookie, int flags, struct virgl_renderer_callbacks *cb) {
   int (*f)(void *, int, struct virgl_renderer_callbacks *) = dlsym(RTLD_NEXT, "virgl_renderer_init");
   real = *cb; mine = *cb; mine.write_fence = w_fence; if (cb->version >= 3 && cb->write_context_fence) mine.write_context_fence = w_ctx_fence;
   LOG("init flags=0x%x version=%d\n", flags, cb->version);
   return f(cookie, flags, &mine);
}
int virgl_renderer_create_fence(int client_fence_id, uint32_t ctx_id) {
   int (*f)(int, uint32_t) = dlsym(RTLD_NEXT, "virgl_renderer_create_fence");
   LOG("create_fence %d ctx=%u\n", client_fence_id, ctx_id); return f(client_fence_id, ctx_id);
}
int virgl_renderer_context_create_fence(uint32_t ctx_id, uint32_t flags, uint32_t ring_idx, uint64_t fence_id) {
   int (*f)(uint32_t, uint32_t, uint32_t, uint64_t) = dlsym(RTLD_NEXT, "virgl_renderer_context_create_fence");
   LOG("ctx_create_fence ctx=%u ring=%u %llu\n", ctx_id, ring_idx, (unsigned long long)fence_id); return f(ctx_id, flags, ring_idx, fence_id);
}
void virgl_renderer_poll(void) {
   void (*f)(void) = dlsym(RTLD_NEXT, "virgl_renderer_poll");
   LOG("poll\n"); f();
}
