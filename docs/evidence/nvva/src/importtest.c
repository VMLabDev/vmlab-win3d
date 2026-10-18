/*
 * importtest: can another process import a dma-buf of NVIDIA memory and bind
 * it to a LINEAR image, as dwm does with a display image another process
 * created? (Every Venus guest process has its own context and render server.)
 *
 *   importtest <pci-bus-hex> <export-type> [import-type]
 *   (internally: importtest --child <bus> <fd> [import-type])
 *
 * The parent allocates 1280x800 B8G8R8A8 LINEAR image memory from
 * <export-type> with dma-buf export, binds it to an image, and forks. The
 * child creates its own instance and device, queries the fd's memory types,
 * imports it as [import-type] (default: Yttrium's choice, the first
 * device-local type allowed by both the image and the fd, else the first
 * allowed type) and binds it to its own identical LINEAR image.
 * Build: cc -O2 -o importtest importtest.c -lvulkan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

static VkPhysicalDevice pd;
static VkDevice dev;
static VkPhysicalDeviceMemoryProperties mp;

static void open_device(unsigned bus)
{
   VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
   VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
   VkInstance inst;
   if (vkCreateInstance(&ici, NULL, &inst)) exit(1);
   uint32_t n = 16;
   VkPhysicalDevice pds[16];
   vkEnumeratePhysicalDevices(inst, &n, pds);
   for (uint32_t i = 0; i < n; i++) {
      VkPhysicalDevicePCIBusInfoPropertiesEXT pci = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT };
      VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &pci };
      vkGetPhysicalDeviceProperties2(pds[i], &p2);
      if (pci.pciBus == bus) pd = pds[i];
   }
   if (!pd) exit(1);
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
   const char *exts[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf" };
   VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
                              .enabledExtensionCount = 2, .ppEnabledExtensionNames = exts };
   if (vkCreateDevice(pd, &dci, NULL, &dev)) exit(1);
}

static VkImage make_image(void)
{
   VkExternalMemoryImageCreateInfo ext = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
   VkImageCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &ext, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_B8G8R8A8_UNORM, .extent = { 1280, 800, 1 }, .mipLevels = 1, .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
   VkImage img;
   VkResult r = vkCreateImage(dev, &ci, NULL, &img);
   if (r) { printf("vkCreateImage %d\n", r); exit(1); }
   return img;
}

static int child_main(unsigned bus, int fd, int argc, char **argv);

int main(int argc, char **argv)
{
   if (argc >= 4 && !strcmp(argv[1], "--child"))
      return child_main(strtoul(argv[2], NULL, 16), atoi(argv[3]), argc - 2, argv + 2);
   if (argc < 3) { fprintf(stderr, "usage: %s <pci-bus-hex> <export-type> [import-type]\n", argv[0]); return 2; }
   unsigned bus = strtoul(argv[1], NULL, 16);
   uint32_t et = strtoul(argv[2], NULL, 0);
   open_device(bus);
   VkImage img = make_image();
   VkMemoryRequirements req;
   vkGetImageMemoryRequirements(dev, img, &req);
   printf("exporter: image memoryTypeBits 0x%x size %llu, allocating type %u (flags 0x%x)\n", req.memoryTypeBits,
          (unsigned long long)req.size, et, mp.memoryTypes[et].propertyFlags);
   VkExportMemoryAllocateInfo exp = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                                      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
   VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &exp, .allocationSize = req.size, .memoryTypeIndex = et };
   VkDeviceMemory m;
   VkResult r = vkAllocateMemory(dev, &mai, NULL, &m);
   printf("exporter: vkAllocateMemory %d\n", r);
   if (r) return 1;
   printf("exporter: vkBindImageMemory %d\n", vkBindImageMemory(dev, img, m, 0));
   PFN_vkGetMemoryFdKHR getfd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdKHR");
   VkMemoryGetFdInfoKHR gi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, .memory = m,
                               .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
   int fd = -1;
   printf("exporter: vkGetMemoryFdKHR %d\n", getfd(dev, &gi, &fd));
   fflush(stdout);

   pid_t child = fork();
   if (child == 0) {
      /* The NVIDIA driver does not survive fork without exec. */
      char fds[16];
      snprintf(fds, sizeof(fds), "%d", fd);
      execl("/proc/self/exe", argv[0], "--child", argv[1], fds, argc > 3 ? argv[3] : NULL, (char *)NULL);
      _exit(127);
   }
   int st;
   waitpid(child, &st, 0);
   printf("child exit %d\n", WEXITSTATUS(st));
   return 0;
}

static int child_main(unsigned bus, int fd, int argc, char **argv)
{
   VkResult r;
   {
      open_device(bus);
      VkImage cimg = make_image();
      VkMemoryRequirements creq;
      vkGetImageMemoryRequirements(dev, cimg, &creq);
      PFN_vkGetMemoryFdPropertiesKHR getprops = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdPropertiesKHR");
      VkMemoryFdPropertiesKHR fp = { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
      r = getprops(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fp);
      uint32_t bits = creq.memoryTypeBits & fp.memoryTypeBits, it = UINT32_MAX;
      if (argc > 3) it = strtoul(argv[3], NULL, 0);
      for (uint32_t i = 0; it == UINT32_MAX && i < mp.memoryTypeCount; i++)
         if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) it = i;
      for (uint32_t i = 0; it == UINT32_MAX && i < mp.memoryTypeCount; i++)
         if (bits & (1u << i)) it = i;
      printf("importer: fd memoryTypeBits 0x%x (vk %d), image bits 0x%x, both 0x%x, importing as type %d\n",
             fp.memoryTypeBits, r, creq.memoryTypeBits, bits, (int)it);
      if (it == UINT32_MAX) return 3;
      VkImportMemoryFdInfoKHR imp = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
                                      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = dup(fd) };
      VkMemoryAllocateInfo iai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &imp,
                                   .allocationSize = creq.size, .memoryTypeIndex = it };
      VkDeviceMemory im;
      r = vkAllocateMemory(dev, &iai, NULL, &im);
      printf("importer: import vkAllocateMemory %d\n", r);
      if (!r) printf("importer: vkBindImageMemory %d\n", vkBindImageMemory(dev, cimg, im, 0));
      fflush(stdout);
      return r ? 1 : 0;
   }
}
