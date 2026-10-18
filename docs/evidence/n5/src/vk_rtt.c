/* vk_rtt <iterations> <out.txt>: Venus submit round trip. Each iteration
 * submits one (empty or tiny) command buffer with a fence and waits for it. */
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static double ms(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER f) { return 1000.0 * (b.QuadPart - a.QuadPart) / f.QuadPart; }
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }

int main(int argc, char **argv)
{
   int n = argc > 1 ? atoi(argv[1]) : 200;
   int pause_ms = argc > 3 ? atoi(argv[3]) : -1;
   FILE *out = argc > 2 ? fopen(argv[2], "w") : stdout;
   setvbuf(out, NULL, _IONBF, 0);
   fprintf(out, "start\n");
   HMODULE m = LoadLibraryA("vulkan-1.dll");
   PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(m, "vkGetInstanceProcAddr");
#define G(name) PFN_##name name = (PFN_##name)gipa(inst, #name)
   VkInstance inst = NULL;
   PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vk_rtt", 1, NULL, 0, VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app};
   if (vkCreateInstance(&ici, NULL, &inst)) { fprintf(out, "no instance\n"); return 1; }
   G(vkEnumeratePhysicalDevices); G(vkCreateDevice); G(vkGetDeviceQueue); G(vkCreateFence); G(vkResetFences);
   G(vkWaitForFences); G(vkQueueSubmit); G(vkCreateCommandPool); G(vkAllocateCommandBuffers);
   G(vkBeginCommandBuffer); G(vkEndCommandBuffer); G(vkGetPhysicalDeviceProperties); G(vkGetFenceStatus);
   fprintf(out, "instance\n");
   VkPhysicalDevice pd; uint32_t c = 1; vkEnumeratePhysicalDevices(inst, &c, &pd);
   VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd, &props);
   fprintf(out, "device: %s\n", props.deviceName);
   float pr = 1; VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &pr};
   VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &q};
   VkDevice dev; if (vkCreateDevice(pd, &dci, NULL, &dev)) { fprintf(out, "no device\n"); return 1; }
   fprintf(out, "device ok\n");
   VkQueue queue; vkGetDeviceQueue(dev, 0, 0, &queue);
   VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence; vkCreateFence(dev, &fci, NULL, &fence);
   VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0};
   VkCommandPool pool; vkCreateCommandPool(dev, &cpci, NULL, &pool);
   VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
   VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
   VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   vkBeginCommandBuffer(cb, &bi); vkEndCommandBuffer(cb);
   LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f);
   double *v = calloc(n, sizeof(double));
   for (int mode = 0; mode < 2; mode++) {
      int polls = 0;
      for (int i = 0; i < n; i++) {
         VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb};
         vkResetFences(dev, 1, &fence);
         QueryPerformanceCounter(&a);
         vkQueueSubmit(queue, 1, &si, fence);
         if (mode == 0)
            { VkResult r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull); if (r) { fprintf(out, "wait %d -> %d\n", i, r); break; } }
         else
            while (vkGetFenceStatus(dev, fence) == VK_NOT_READY && polls < 50000000) polls++;
         QueryPerformanceCounter(&b);
         v[i] = ms(a, b, f);
         if (pause_ms >= 0) Sleep(pause_ms);
      }
      qsort(v, n, sizeof(double), cmp);
      double s = 0; for (int i = 0; i < n; i++) s += v[i];
      fprintf(out, "%s: n=%d mean=%.3f p50=%.3f p95=%.3f max=%.3f min=%.3f polls=%d\n",
              mode ? "submit+spin_getfencestatus" : "submit+waitforfences", n, s / n, v[n / 2], v[n * 95 / 100], v[n - 1], v[0], polls);
   }
   fclose(out);
   return 0;
}
