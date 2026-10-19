/* fbtest: do host GPU writes to a Venus-style feedback buffer reach the CPU?
 *
 * Venus (guest Mesa) keeps fence/semaphore/event/query status in a buffer in
 * the first HOST_COHERENT memory type; a vkCmdFillBuffer appended to each
 * submission writes the status word and the guest CPU reads it through the
 * virtio-gpu hostmem BAR. On the host, vkr allocates that memory exportable
 * (dma-buf when the driver can, otherwise opaque fd) and QEMU mmaps the
 * dma-buf into the BAR.
 *
 * For every host-visible memory type this test runs N rounds of
 *   CPU: slot = 0 (and read it, so a cached view holds the line)
 *   GPU: vkCmdFillBuffer(slot, round) + transfer->host barrier, fence wait
 *   CPU: read slot through the view; count rounds where it is not `round`
 * through three views of the GPU-written pages:
 *   map    vkMapMemory of a plain allocation
 *   dmabuf mmap of the exported dma-buf fd, as QEMU maps a blob
 *   hostwb a malloc'd (write-back cached) page imported with
 *          VK_EXT_external_memory_host into the type; this is how the CPU
 *          sees the pages when they are mapped cacheable, e.g. into a KVM
 *          guest on an Intel host, where EPT ignores the guest's PAT.
 *
 * Usage: fbtest [device-name-substring] [rounds]
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define CHECK(x)                                                              \
   do {                                                                       \
      VkResult r_ = (x);                                                      \
      if (r_ != VK_SUCCESS) {                                                 \
         fprintf(stderr, "%s:%d %s = %d\n", __FILE__, __LINE__, #x, r_);       \
         exit(1);                                                             \
      }                                                                       \
   } while (0)

static VkDevice dev;
static VkQueue queue;
static VkCommandPool pool;
static VkPhysicalDeviceMemoryProperties mp;
static PFN_vkGetMemoryFdKHR pGetMemoryFdKHR;
static PFN_vkGetMemoryHostPointerPropertiesEXT pGetMemoryHostPointerProperties;
static VkDeviceSize host_align = 4096;

static double
now_us(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static VkBuffer
make_buffer(VkDeviceSize size, VkExternalMemoryHandleTypeFlags ext)
{
   VkExternalMemoryBufferCreateInfo emb = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      .handleTypes = ext,
   };
   VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = ext ? &emb : NULL,
      .size = size,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
   };
   VkBuffer b;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &b));
   return b;
}

/* Runs the rounds; `view` is the CPU pointer to the slot. Returns misses. */
static int
run_rounds(VkBuffer buf, volatile uint32_t *view, int rounds, int *late)
{
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

   int misses = 0;
   *late = 0;
   for (int i = 1; i <= rounds; i++) {
      *view = 0;
      (void)*view;

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
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0,
                           NULL);
      CHECK(vkEndCommandBuffer(cb));
      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cb,
      };
      CHECK(vkQueueSubmit(queue, 1, &si, fence));
      CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull));
      CHECK(vkResetFences(dev, 1, &fence));

      if (*view != (uint32_t)i) {
         misses++;
         double t0 = now_us();
         while (*view != (uint32_t)i && now_us() - t0 < 2000)
            ;
         if (*view == (uint32_t)i)
            (*late)++;
      }
   }
   vkDestroyFence(dev, fence, NULL);
   vkFreeCommandBuffers(dev, pool, 1, &cb);
   return misses;
}

static void
test_type(uint32_t t, int rounds)
{
   const VkMemoryPropertyFlags f = mp.memoryTypes[t].propertyFlags;
   printf("type %u flags 0x%02x heap %u%s%s%s%s\n", t, f,
          mp.memoryTypes[t].heapIndex,
          f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? " DEVICE_LOCAL" : "",
          f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? " HOST_VISIBLE" : "",
          f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? " HOST_COHERENT" : "",
          f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? " HOST_CACHED" : "");
   const VkDeviceSize size = 4096;
   int late;

   /* map: plain allocation, vkMapMemory */
   {
      VkBuffer b = make_buffer(size, 0);
      VkMemoryRequirements mr;
      vkGetBufferMemoryRequirements(dev, b, &mr);
      if (mr.memoryTypeBits & (1u << t)) {
         VkMemoryAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size,
            .memoryTypeIndex = t,
         };
         VkDeviceMemory m;
         VkResult r = vkAllocateMemory(dev, &ai, NULL, &m);
         if (r != VK_SUCCESS) {
            printf("  map     vkAllocateMemory %d\n", r);
            vkDestroyBuffer(dev, b, NULL);
            goto dmabuf;
         }
         CHECK(vkBindBufferMemory(dev, b, m, 0));
         void *p;
         CHECK(vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, &p));
         int miss = run_rounds(b, p, rounds, &late);
         printf("  map     misses %d/%d (late %d)\n", miss, rounds, late);
         vkUnmapMemory(dev, m);
         vkFreeMemory(dev, m, NULL);
      } else {
         printf("  map     buffer cannot use this type\n");
      }
      vkDestroyBuffer(dev, b, NULL);
   }

   /* dmabuf: exportable allocation as vkr makes it; mmap of the fd as QEMU */
dmabuf:
   {
      const VkExternalMemoryHandleTypeFlagBits ht =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      VkBuffer b = make_buffer(size, 0);
      VkMemoryRequirements mr;
      vkGetBufferMemoryRequirements(dev, b, &mr);
      VkExportMemoryAllocateInfo ei = {
         .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
         .handleTypes = ht,
      };
      VkMemoryAllocateInfo ai = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .pNext = &ei,
         .allocationSize = mr.size,
         .memoryTypeIndex = t,
      };
      VkDeviceMemory m;
      if ((mr.memoryTypeBits & (1u << t)) &&
          vkAllocateMemory(dev, &ai, NULL, &m) == VK_SUCCESS) {
         CHECK(vkBindBufferMemory(dev, b, m, 0));
         VkMemoryGetFdInfoKHR gi = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory = m,
            .handleType = ht,
         };
         int fd = -1;
         VkResult r = pGetMemoryFdKHR(dev, &gi, &fd);
         void *p = MAP_FAILED;
         if (r == VK_SUCCESS)
            p = mmap(NULL, mr.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
         if (p != MAP_FAILED) {
            int miss = run_rounds(b, p, rounds, &late);
            printf("  dmabuf  misses %d/%d (late %d)\n", miss, rounds, late);
            munmap(p, mr.size);
         } else {
            printf("  dmabuf  export %d, mmap failed\n", r);
         }
         if (fd >= 0)
            close(fd);
         vkFreeMemory(dev, m, NULL);
      } else {
         printf("  dmabuf  exportable allocation not possible\n");
      }
      vkDestroyBuffer(dev, b, NULL);
   }

   /* hostwb: write-back host page imported into the type */
   if (pGetMemoryHostPointerProperties) {
      const VkExternalMemoryHandleTypeFlagBits ht =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      void *host = NULL;
      if (posix_memalign(&host, host_align, host_align))
         exit(1);
      memset(host, 0, host_align);
      VkMemoryHostPointerPropertiesEXT hp = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
      };
      CHECK(pGetMemoryHostPointerProperties(dev, ht, host, &hp));
      VkBuffer b = make_buffer(host_align, ht);
      VkMemoryRequirements mr;
      vkGetBufferMemoryRequirements(dev, b, &mr);
      if ((hp.memoryTypeBits & mr.memoryTypeBits & (1u << t))) {
         VkImportMemoryHostPointerInfoEXT ii = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .handleType = ht,
            .pHostPointer = host,
         };
         VkMemoryAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &ii,
            .allocationSize = host_align,
            .memoryTypeIndex = t,
         };
         VkDeviceMemory m;
         CHECK(vkAllocateMemory(dev, &ai, NULL, &m));
         CHECK(vkBindBufferMemory(dev, b, m, 0));
         int miss = run_rounds(b, host, rounds, &late);
         printf("  hostwb  misses %d/%d (late %d)\n", miss, rounds, late);
         vkFreeMemory(dev, m, NULL);
      } else {
         printf("  hostwb  host pointer import not allowed (bits 0x%x)\n",
                hp.memoryTypeBits);
      }
      vkDestroyBuffer(dev, b, NULL);
      free(host);
   }
}

int
main(int argc, char **argv)
{
   const char *want = argc > 1 ? argv[1] : "";
   const int rounds = argc > 2 ? atoi(argv[2]) : 200;

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
   uint32_t n = 8;
   VkPhysicalDevice pds[8];
   vkEnumeratePhysicalDevices(inst, &n, pds);
   VkPhysicalDevice pd = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < n; i++) {
      VkPhysicalDeviceProperties p;
      vkGetPhysicalDeviceProperties(pds[i], &p);
      if (!pd && strstr(p.deviceName, want)) {
         pd = pds[i];
         printf("device %s, driver 0x%x\n", p.deviceName, p.driverVersion);
      }
   }
   if (!pd) {
      fprintf(stderr, "no device matching '%s'\n", want);
      return 1;
   }
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);

   VkPhysicalDeviceExternalMemoryHostPropertiesEXT hostp = {
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT,
   };
   VkPhysicalDeviceProperties2 p2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &hostp,
   };
   vkGetPhysicalDeviceProperties2(pd, &p2);
   if (hostp.minImportedHostPointerAlignment)
      host_align = hostp.minImportedHostPointerAlignment;

   /* The query vkr_physical_device_init_memory_properties makes to choose
    * between dma-buf and opaque fd export for host-visible memory. */
   const VkExternalMemoryHandleTypeFlagBits hts[2] = {
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
   };
   for (int i = 0; i < 2; i++) {
      VkPhysicalDeviceExternalBufferInfo ebi = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
         .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         .handleType = hts[i],
      };
      VkExternalBufferProperties ebp = {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES,
      };
      vkGetPhysicalDeviceExternalBufferProperties(pd, &ebi, &ebp);
      printf("export %s: features 0x%x exportFrom 0x%x compatible 0x%x\n",
             i ? "opaque_fd" : "dma_buf",
             ebp.externalMemoryProperties.externalMemoryFeatures,
             ebp.externalMemoryProperties.exportFromImportedHandleTypes,
             ebp.externalMemoryProperties.compatibleHandleTypes);
   }

   const char *exts[] = {
      VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
      VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
      VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
   };
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
      .enabledExtensionCount = 3,
      .ppEnabledExtensionNames = exts,
   };
   CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
   vkGetDeviceQueue(dev, 0, 0, &queue);
   pGetMemoryFdKHR =
      (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdKHR");
   pGetMemoryHostPointerProperties =
      (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(
         dev, "vkGetMemoryHostPointerPropertiesEXT");
   VkCommandPoolCreateInfo pci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = 0,
   };
   CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));

   for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
      if (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
         test_type(t, rounds);
   }

   vkDestroyCommandPool(dev, pool, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   return 0;
}
