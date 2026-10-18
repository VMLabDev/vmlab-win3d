/*
 * bar1probe: what happens on the host when the NVIDIA DEVICE_LOCAL |
 * HOST_VISIBLE heap (the 246 MiB BAR window without resizable BAR) fills.
 *
 *   bar1probe <pci-bus-hex> <chunk-MiB[,chunk-MiB...]> <max-MiB> <mode> [hold-seconds]
 *
 * Chunk sizes are taken in order; the last one repeats.
 *
 * mode:
 *   alloc   vkAllocateMemory only
 *   map     vkAllocateMemory + vkMapMemory (what a host app does)
 *   dmabuf  vkAllocateMemory with dma-buf export + vkGetMemoryFdKHR + mmap
 *           of the fd, then a write to every page (what virglrenderer and
 *           QEMU do for a Venus guest's host-visible blob)
 *   export  as dmabuf, without the mmap (an exported blob the guest never
 *           maps, such as a shared device-local image)
 *
 * BAR1PROBE_TYPE=<index> picks that memory type instead of the first
 * device-local, host-visible one (for example a system-memory type).
 *
 * After each chunk it prints the heap's VK_EXT_memory_budget usage and
 * budget, so the card-wide BAR1 use can be compared with nvidia-smi.
 * Build: cc -O2 -o bar1probe bar1probe.c -lvulkan
 */
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

static sigjmp_buf jb;
static void on_sigbus(int s) { (void)s; siglongjmp(jb, 1); }

int main(int argc, char **argv)
{
   if (argc < 5) {
      fprintf(stderr, "usage: %s <pci-bus-hex> <chunk-MiB> <max-MiB> alloc|map|dmabuf|export [hold-s]\n", argv[0]);
      return 2;
   }
   unsigned bus = strtoul(argv[1], NULL, 16);
   VkDeviceSize chunks[64];
   int nchunks = 0;
   for (char *t = strtok(argv[2], ","); t && nchunks < 64; t = strtok(NULL, ","))
      chunks[nchunks++] = strtoull(t, NULL, 0) << 20;
   VkDeviceSize chunk = chunks[0];
   VkDeviceSize max = strtoull(argv[3], NULL, 0) << 20;
   const char *mode = argv[4];
   int hold = argc > 5 ? atoi(argv[5]) : 0;
   int export_only = !strcmp(mode, "export");
   int dmabuf = !strcmp(mode, "dmabuf") || export_only;
   int map = !strcmp(mode, "map");

   VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
   VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
   VkInstance inst;
   if (vkCreateInstance(&ici, NULL, &inst)) return 1;
   uint32_t n = 16;
   VkPhysicalDevice pds[16];
   vkEnumeratePhysicalDevices(inst, &n, pds);
   VkPhysicalDevice pd = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < n; i++) {
      VkPhysicalDevicePCIBusInfoPropertiesEXT pci = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT };
      VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &pci };
      vkGetPhysicalDeviceProperties2(pds[i], &p2);
      if (pci.pciBus == bus) {
         pd = pds[i];
         printf("device %s bus %02x\n", p2.properties.deviceName, bus);
      }
   }
   if (!pd) { fprintf(stderr, "no device on bus %x\n", bus); return 1; }

   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   uint32_t type = UINT32_MAX;
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
      if ((mp.memoryTypes[i].propertyFlags & want) == want) { type = i; break; }
   if (getenv("BAR1PROBE_TYPE"))
      type = strtoul(getenv("BAR1PROBE_TYPE"), NULL, 0);
   if (type == UINT32_MAX) { fprintf(stderr, "no device-local host-visible type\n"); return 1; }
   uint32_t heap = mp.memoryTypes[type].heapIndex;
   printf("type %u heap %u size %llu MiB\n", type, heap, (unsigned long long)(mp.memoryHeaps[heap].size >> 20));

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
   const char *exts[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf", "VK_EXT_memory_budget" };
   VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
                              .enabledExtensionCount = 3, .ppEnabledExtensionNames = exts };
   VkDevice dev;
   VkResult r = vkCreateDevice(pd, &dci, NULL, &dev);
   if (r) { fprintf(stderr, "vkCreateDevice %d\n", r); return 1; }
   PFN_vkGetMemoryFdKHR getfd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdKHR");

   signal(SIGBUS, on_sigbus);
   size_t cap = max / (1 << 20) + 1, got = 0;
   VkDeviceMemory *mems = calloc(cap, sizeof(*mems));
   VkDeviceSize total = 0;
   while (total < max) {
      chunk = chunks[got < (size_t)nchunks ? got : (size_t)nchunks - 1];
      VkExportMemoryAllocateInfo exp = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                                         .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
      VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = dmabuf ? &exp : NULL,
                                   .allocationSize = chunk, .memoryTypeIndex = type };
      VkDeviceMemory m;
      r = vkAllocateMemory(dev, &mai, NULL, &m);
      const char *what = "ok";
      if (r) { printf("chunk %zu: vkAllocateMemory %d\n", got, r); break; }
      mems[got++] = m;
      total += chunk;
      if (map) {
         void *p;
         r = vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, &p);
         if (r) what = "vkMapMemory failed";
         else memset(p, 0x5a, chunk);
      } else if (dmabuf) {
         VkMemoryGetFdInfoKHR gi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, .memory = m,
                                     .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
         int fd = -1;
         r = getfd(dev, &gi, &fd);
         if (r) what = "vkGetMemoryFdKHR failed";
         else if (!export_only) {
            void *p = mmap(NULL, chunk, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (p == MAP_FAILED) { what = "mmap failed"; printf("  mmap errno %d %s\n", errno, strerror(errno)); }
            else if (sigsetjmp(jb, 1)) what = "SIGBUS on first touch";
            else memset(p, 0x5a, chunk);
         }
      }
      VkPhysicalDeviceMemoryBudgetPropertiesEXT b = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
      VkPhysicalDeviceMemoryProperties2 mp2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, .pNext = &b };
      vkGetPhysicalDeviceMemoryProperties2(pd, &mp2);
      printf("chunk %zu total %llu MiB: %s (vk %d); heap usage %llu MiB budget %llu MiB\n", got - 1,
             (unsigned long long)(total >> 20), what, r,
             (unsigned long long)(b.heapUsage[heap] >> 20), (unsigned long long)(b.heapBudget[heap] >> 20));
      fflush(stdout);
      if (strcmp(what, "ok")) break;
   }
   if (hold) { printf("holding %d s\n", hold); fflush(stdout); sleep(hold); }
   for (size_t i = 0; i < got; i++) vkFreeMemory(dev, mems[i], NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   return 0;
}
