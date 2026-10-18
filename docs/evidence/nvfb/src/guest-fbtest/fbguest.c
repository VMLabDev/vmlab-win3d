/* fbguest: in a Venus guest, do host GPU writes to host-visible memory reach
 * the guest CPU?
 *
 * This is what Venus's feedback does: a vkCmdFillBuffer that the host GPU
 * executes writes a status word into host-visible memory, which the guest
 * reads through its mapping of the virtio-gpu hostmem BAR. For every
 * host-visible memory type the test runs N rounds of
 *   CPU: slot = 0, read it back
 *   GPU: vkCmdFillBuffer(slot, round) + transfer->host barrier, fence wait
 *   CPU: read slot; a miss when it is not `round`
 * and on a miss it also tries vkInvalidateMappedMemoryRanges and a 50 ms
 * spin, and reports whether either made the write visible. Two more passes
 * flush the CPU cache line around the GPU write (see the modes below); only
 * the first pass decides the RESULT line.
 *
 * Usage: fbguest.exe [rounds] [log]   output on stdout, or in log; exit 1
 * on any miss. Run it in the user's session: Vulkan does not enumerate
 * devices from session 0.
 */
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <emmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

#define FNS(X)                                                                \
   X(vkCreateInstance) X(vkEnumeratePhysicalDevices)                          \
   X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceMemoryProperties)    \
   X(vkCreateDevice) X(vkGetDeviceQueue) X(vkCreateCommandPool)               \
   X(vkAllocateCommandBuffers) X(vkCreateFence) X(vkCreateBuffer)             \
   X(vkGetBufferMemoryRequirements) X(vkAllocateMemory)                       \
   X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkFreeMemory)      \
   X(vkDestroyBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer)           \
   X(vkCmdFillBuffer) X(vkCmdPipelineBarrier) X(vkQueueSubmit)                \
   X(vkWaitForFences) X(vkResetFences) X(vkResetCommandBuffer)                \
   X(vkInvalidateMappedMemoryRanges) X(vkDestroyFence)                        \
   X(vkDestroyCommandPool) X(vkDestroyDevice) X(vkDestroyInstance)

#define DECL(n) static PFN_##n n;
FNS(DECL)

#define CHECK(x)                                                              \
   do {                                                                       \
      VkResult r_ = (x);                                                      \
      if (r_ != VK_SUCCESS) {                                                 \
         printf("FAIL %s:%d %s = %d\n", __FILE__, __LINE__, #x, r_);           \
         exit(2);                                                             \
      }                                                                       \
   } while (0)

static double
now_us(void)
{
   static LARGE_INTEGER f;
   LARGE_INTEGER c;
   if (!f.QuadPart)
      QueryPerformanceFrequency(&f);
   QueryPerformanceCounter(&c);
   return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

int
main(int argc, char **argv)
{
   const int rounds = argc > 1 ? atoi(argv[1]) : 200;
   if (argc > 2 && !freopen(argv[2], "w", stdout))
      return 2;
   setvbuf(stdout, NULL, _IONBF, 0);

   HMODULE lib = LoadLibraryExW(L"vulkan-1.dll", NULL,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
   if (!lib) {
      printf("FAIL no vulkan-1.dll\n");
      return 2;
   }
   PFN_vkGetInstanceProcAddr gipa =
      (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(lib,
                                                        "vkGetInstanceProcAddr");
   vkCreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");

   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = VK_API_VERSION_1_1,
   };
   VkInstanceCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   VkInstance inst;
   CHECK(vkCreateInstance(&ici, NULL, &inst));
#define LOAD(n) n = (PFN_##n)gipa(inst, #n);
   FNS(LOAD)

   uint32_t n = 1;
   VkPhysicalDevice pd;
   VkResult er = vkEnumeratePhysicalDevices(inst, &n, &pd);
   if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) {
      printf("FAIL no physical device\n");
      return 2;
   }
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(pd, &props);
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   printf("device %s, driver 0x%x\n", props.deviceName, props.driverVersion);

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = 0,
      .queueCount = 1,
      .pQueuePriorities = &prio,
   };
   VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &qci,
   };
   /* Device creation time, for the cost of Venus's feedback self-test
    * (patches/mesa/0066): the median of 11 create/destroy pairs. */
   VkDevice dev;
   double times[11];
   for (int i = 0; i < 11; i++) {
      double t0 = now_us();
      CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
      times[i] = now_us() - t0;
      if (i < 10)
         vkDestroyDevice(dev, NULL);
   }
   for (int i = 0; i < 11; i++)
      for (int j = i + 1; j < 11; j++)
         if (times[j] < times[i]) {
            double t = times[i];
            times[i] = times[j];
            times[j] = t;
         }
   printf("vkCreateDevice median %.0f us (min %.0f, max %.0f, 11 runs)\n",
          times[5], times[0], times[10]);
   VkQueue queue;
   vkGetDeviceQueue(dev, 0, 0, &queue);
   VkCommandPoolCreateInfo pci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
   };
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer cb;
   CHECK(vkAllocateCommandBuffers(dev, &cai, &cb));
   VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   VkFence fence;
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));

   int total_misses = 0;
   for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
      const VkMemoryPropertyFlags f = mp.memoryTypes[t].propertyFlags;
      if (!(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
         continue;
      printf("type %u flags 0x%02x heap %u%s%s%s%s\n", t, f,
             mp.memoryTypes[t].heapIndex,
             f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? " DEVICE_LOCAL" : "",
             f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? " HOST_VISIBLE" : "",
             f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? " HOST_COHERENT" : "",
             f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? " HOST_CACHED" : "");

      VkBufferCreateInfo bci = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = 4096,
         .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      };
      VkBuffer buf;
      CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
      VkMemoryRequirements mr;
      vkGetBufferMemoryRequirements(dev, buf, &mr);
      if (!(mr.memoryTypeBits & (1u << t))) {
         printf("  buffer cannot use this type\n");
         vkDestroyBuffer(dev, buf, NULL);
         continue;
      }
      VkMemoryAllocateInfo ai = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = mr.size,
         .memoryTypeIndex = t,
      };
      VkDeviceMemory mem;
      CHECK(vkAllocateMemory(dev, &ai, NULL, &mem));
      CHECK(vkBindBufferMemory(dev, buf, mem, 0));
      void *ptr;
      CHECK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &ptr));
      volatile uint32_t *slot = ptr;

      /* primed:  the CPU writes 0 and reads it back before the submission,
       *          as Venus resets and polls a feedback slot; this pass is the
       *          verdict.
       * flushed: the CPU writes 0 and clflushes the line, and does not read
       *          it until the fence has signalled.
       * clflush: as primed, and clflush again before the final read.
       * If primed misses and flushed and clflush do not, the guest's view of
       * the memory is cached while the GPU's writes bypass the CPU caches.
       */
      static const char *const mode_names[] = { "primed", "flushed",
                                                "clflush" };
      for (int mode = 0; mode < 3; mode++) {
      int misses = 0, by_invalidate = 0, by_spin = 0, never = 0;
      double t_start = now_us();
      for (int i = 1; i <= rounds; i++) {
         *slot = 0;
         if (mode) {
            _mm_clflush((const void *)slot);
            _mm_mfence();
         }
         if (mode != 1)
            (void)*slot;
         VkCommandBufferBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
         };
         CHECK(vkBeginCommandBuffer(cb, &bi));
         vkCmdFillBuffer(cb, buf, 0, 4, (uint32_t)i);
         VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
         };
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL,
                              0, NULL);
         CHECK(vkEndCommandBuffer(cb));
         VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
         };
         CHECK(vkQueueSubmit(queue, 1, &si, fence));
         CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull));
         CHECK(vkResetFences(dev, 1, &fence));
         CHECK(vkResetCommandBuffer(cb, 0));

         if (mode == 2) {
            _mm_clflush((const void *)slot);
            _mm_mfence();
         }
         const uint32_t seen = *slot;
         if (seen == (uint32_t)i)
            continue;
         misses++;
         if (misses <= 3)
            printf("  %s round %d: read 0x%x\n", mode_names[mode], i, seen);
         VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = mem,
            .size = VK_WHOLE_SIZE,
         };
         CHECK(vkInvalidateMappedMemoryRanges(dev, 1, &range));
         if (*slot == (uint32_t)i) {
            by_invalidate++;
            continue;
         }
         double s0 = now_us();
         while (*slot != (uint32_t)i && now_us() - s0 < 50000)
            ;
         if (*slot == (uint32_t)i)
            by_spin++;
         else
            never++;
      }
      printf("  %-7s misses %d/%d (visible after invalidate %d, after 50 ms "
             "spin %d, never %d), %.0f us per round\n",
             mode_names[mode], misses, rounds, by_invalidate, by_spin, never,
             (now_us() - t_start) / rounds);
      if (mode == 0)
         total_misses += misses;
      }
      vkUnmapMemory(dev, mem);
      vkFreeMemory(dev, mem, NULL);
      vkDestroyBuffer(dev, buf, NULL);
   }

   vkDestroyFence(dev, fence, NULL);
   vkDestroyCommandPool(dev, pool, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   printf("RESULT %s\n", total_misses ? "FAIL" : "PASS");
   return total_misses ? 1 : 0;
}
