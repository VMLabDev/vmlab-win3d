/* Host Vulkan fence wake latency, the way virglrenderer's vkr sync thread
 * sees it: vkQueueSubmit(queue, 0, NULL, fence) after some work, and another
 * thread in vkWaitForFences(fence, 3 s). Usage: vkfence [device-index] */
#include <vulkan/vulkan.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static VkDevice dev; static VkFence fence; static volatile double tw; static volatile int go, quit;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static void *thr(void *a) {
   for (;;) {
      pthread_mutex_lock(&mu); while (!go && !quit) pthread_cond_wait(&cv, &mu); go = 0; pthread_mutex_unlock(&mu);
      if (quit) break;
      vkWaitForFences(dev, 1, &fence, VK_TRUE, 3000000000ull);
      pthread_mutex_lock(&mu); tw = now(); pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
   }
   return NULL;
}
int main(int argc, char **argv) {
   VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkfence", 1, NULL, 0, VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app};
   VkInstance inst; if (vkCreateInstance(&ici, NULL, &inst)) return 1;
   uint32_t n = 8; VkPhysicalDevice pds[8]; vkEnumeratePhysicalDevices(inst, &n, pds);
   VkPhysicalDevice pd = pds[argc > 1 ? atoi(argv[1]) : 0];
   VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd, &p); printf("device: %s\n", p.deviceName);
   float pr = 1; VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &pr};
   const char *exts[] = {"VK_KHR_external_fence", "VK_KHR_external_fence_fd"};
   VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &q, 0, NULL, 2, exts};
   vkCreateDevice(pd, &dci, NULL, &dev); VkQueue queue; vkGetDeviceQueue(dev, 0, 0, &queue);
   VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0}; VkCommandPool pool; vkCreateCommandPool(dev, &cpci, NULL, &pool);
   VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1}; VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
   VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bi); vkEndCommandBuffer(cb);
   VkExportFenceCreateInfo efi = {VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO, NULL, VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT};
   VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, (getenv("EXPORT") && *getenv("EXPORT")) ? &efi : NULL};
   printf("fence: %s\n", (getenv("EXPORT") && *getenv("EXPORT")) ? "exportable SYNC_FD (as virglrenderer vkr)" : "plain"); vkCreateFence(dev, &fci, NULL, &fence);
   pthread_t t; pthread_create(&t, NULL, thr, NULL);
   double sum = 0, mx = 0; int over5 = 0, N = 200;
   for (int i = 0; i < N; i++) {
      usleep(4000);
      vkResetFences(dev, 1, &fence);
      VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb};
      double t0 = now();
      vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      vkQueueSubmit(queue, 0, NULL, fence);
      pthread_mutex_lock(&mu); tw = 0; go = 1; pthread_cond_broadcast(&cv); while (!tw) pthread_cond_wait(&cv, &mu); pthread_mutex_unlock(&mu);
      double d = tw - t0; sum += d; if (d > mx) mx = d; over5 += d > 5;
   }
   printf("submit -> vkWaitForFences wake ms: mean=%.3f max=%.3f over5ms=%d/%d\n", sum / N, mx, over5, N);
   pthread_mutex_lock(&mu); quit = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu); pthread_join(t, NULL);
   return 0;
}
