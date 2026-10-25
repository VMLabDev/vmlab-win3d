// vkshare: export a dma-buf image from one Vulkan instance (one Venus
// context) and import it into a second, then read it back.
//
// usage: vkshare [linear|mod|0xMOD] [same|cross] [clear|draw]
//   linear  - DRM_FORMAT_MOD_LINEAR
//   mod     - first non-linear modifier the driver lists for B8G8R8A8
//   cross   - import into a second VkInstance (a second Venus context)
//   same    - import into the exporting device
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s:%d %s -> %d\n", __FILE__, __LINE__, #x, r_); exit(1); } } while (0)

#define W 256
#define H 256
static VkFormat FMT = VK_FORMAT_B8G8R8A8_UNORM;

struct ctx {
    VkInstance inst;
    VkPhysicalDevice pd;
    VkDevice dev;
    VkQueue q;
    uint32_t qf;
    VkCommandPool pool;
    PFN_vkGetMemoryFdKHR getMemoryFd;
    PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProps;
    PFN_vkGetImageDrmFormatModifierPropertiesEXT getModProps;
};

static void ctx_init(struct ctx *c, const char *name)
{
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = name, .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    CK(vkCreateInstance(&ici, NULL, &c->inst));
    uint32_t n = 1;
    VkResult r = vkEnumeratePhysicalDevices(c->inst, &n, &c->pd);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) { fprintf(stderr, "enum pd %d\n", r); exit(1); }
    VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(c->pd, &p);
    fprintf(stderr, "[%s] device %s\n", name, p.deviceName);
    c->qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    const char *exts[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf", "VK_EXT_image_drm_format_modifier", "VK_EXT_queue_family_foreign" };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci, .enabledExtensionCount = 4, .ppEnabledExtensionNames = exts };
    CK(vkCreateDevice(c->pd, &dci, NULL, &c->dev));
    vkGetDeviceQueue(c->dev, 0, 0, &c->q);
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    CK(vkCreateCommandPool(c->dev, &pci, NULL, &c->pool));
    c->getMemoryFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(c->dev, "vkGetMemoryFdKHR");
    c->getMemoryFdProps = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(c->dev, "vkGetMemoryFdPropertiesKHR");
    c->getModProps = (PFN_vkGetImageDrmFormatModifierPropertiesEXT)vkGetDeviceProcAddr(c->dev, "vkGetImageDrmFormatModifierPropertiesEXT");
}

static uint32_t find_mem(struct ctx *c, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(c->pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    fprintf(stderr, "no memory type bits=0x%x want=0x%x\n", bits, want); exit(1);
}

static VkCommandBuffer begin(struct ctx *c)
{
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = c->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    CK(vkAllocateCommandBuffers(c->dev, &ai, &cb));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CK(vkBeginCommandBuffer(cb, &bi));
    return cb;
}

static void submit_wait(struct ctx *c, VkCommandBuffer cb)
{
    CK(vkEndCommandBuffer(cb));
    VkFence f; VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    CK(vkCreateFence(c->dev, &fci, NULL, &f));
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    CK(vkQueueSubmit(c->q, 1, &si, f));
    CK(vkWaitForFences(c->dev, 1, &f, VK_TRUE, UINT64_MAX));
    vkDestroyFence(c->dev, f, NULL);
}

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, uint32_t srcq, uint32_t dstq)
{
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = srcq, .dstQueueFamilyIndex = dstq, .image = img,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

static uint64_t pick_modifier(struct ctx *c, const char *want)
{
    if (!strcmp(want, "linear")) return 0;
    if (!strncmp(want, "0x", 2)) return strtoull(want, NULL, 16);
    VkDrmFormatModifierPropertiesListEXT ml = { .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT };
    VkFormatProperties2 fp = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &ml };
    vkGetPhysicalDeviceFormatProperties2(c->pd, FMT, &fp);
    VkDrmFormatModifierPropertiesEXT mods[64];
    ml.pDrmFormatModifierProperties = mods; if (ml.drmFormatModifierCount > 64) ml.drmFormatModifierCount = 64;
    vkGetPhysicalDeviceFormatProperties2(c->pd, FMT, &fp);
    uint64_t pick = 0;
    for (uint32_t i = 0; i < ml.drmFormatModifierCount; i++) {
        fprintf(stderr, "  modifier 0x%016llx planes %u features 0x%x\n", (unsigned long long)mods[i].drmFormatModifier, mods[i].drmFormatModifierPlaneCount, mods[i].drmFormatModifierTilingFeatures);
        if (!pick && mods[i].drmFormatModifier != 0 && mods[i].drmFormatModifierPlaneCount == 1) pick = mods[i].drmFormatModifier;
    }
    return pick;
}

int main(int argc, char **argv)
{
    const char *modarg = argc > 1 ? argv[1] : "mod";
    int cross = argc > 2 ? !strcmp(argv[2], "cross") : 1;
    setvbuf(stdout, NULL, _IONBF, 0);

    struct ctx a = {0}, bb = {0}, *b;
    ctx_init(&a, "vkshare-exporter");
    if (cross) { ctx_init(&bb, "vkshare-importer"); b = &bb; } else b = &a;

    uint64_t mod = pick_modifier(&a, modarg);
    fprintf(stderr, "using modifier 0x%016llx, %s\n", (unsigned long long)mod, cross ? "cross-context" : "same device");

    // Exporter image.
    VkImageDrmFormatModifierListCreateInfoEXT mlci = { .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT, .drmFormatModifierCount = 1, .pDrmFormatModifiers = &mod };
    VkExternalMemoryImageCreateInfo emi = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, .pNext = &mlci, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &emi, .imageType = VK_IMAGE_TYPE_2D, .format = FMT,
        .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VkImage ia; CK(vkCreateImage(a.dev, &ici, NULL, &ia));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(a.dev, ia, &mr);
    VkMemoryDedicatedAllocateInfo ded = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = ia };
    VkExportMemoryAllocateInfo exp = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, .pNext = &ded, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &exp, .allocationSize = mr.size, .memoryTypeIndex = find_mem(&a, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory ma; CK(vkAllocateMemory(a.dev, &mai, NULL, &ma));
    CK(vkBindImageMemory(a.dev, ia, ma, 0));
    VkImageDrmFormatModifierPropertiesEXT mp = { .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT };
    CK(a.getModProps(a.dev, ia, &mp));
    VkImageSubresource sr = { VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT, 0, 0 };
    VkSubresourceLayout lay; vkGetImageSubresourceLayout(a.dev, ia, &sr, &lay);
    fprintf(stderr, "exporter: size %llu type %u modifier 0x%016llx offset %llu rowPitch %llu size %llu\n",
        (unsigned long long)mr.size, mai.memoryTypeIndex, (unsigned long long)mp.drmFormatModifier,
        (unsigned long long)lay.offset, (unsigned long long)lay.rowPitch, (unsigned long long)lay.size);

    // Fill: left half red, right half blue (a clear, then a smaller clear via attachment-less copy is overkill; use two clears on a staging buffer copy).
    VkCommandBuffer cb = begin(&a);
    barrier(cb, ia, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED);
    VkClearColorValue red = { .float32 = { 1, 0, 0, 1 } };
    VkImageSubresourceRange rng = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cb, ia, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &red, 1, &rng);
    barrier(cb, ia, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, a.qf, VK_QUEUE_FAMILY_FOREIGN_EXT);
    submit_wait(&a, cb);
    fprintf(stderr, "exporter: cleared to red and released to FOREIGN\n");

    int fd;
    VkMemoryGetFdInfoKHR gfi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, .memory = ma, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    CK(a.getMemoryFd(a.dev, &gfi, &fd));
    off_t fdsize = lseek(fd, 0, SEEK_END); lseek(fd, 0, SEEK_SET);
    fprintf(stderr, "exported dma-buf fd %d size %lld\n", fd, (long long)fdsize);

    // Importer image, explicit layout.
    VkSubresourceLayout pl = { .offset = lay.offset, .rowPitch = lay.rowPitch };
    VkImageDrmFormatModifierExplicitCreateInfoEXT eci = { .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT, .drmFormatModifier = mp.drmFormatModifier, .drmFormatModifierPlaneCount = 1, .pPlaneLayouts = &pl };
    VkExternalMemoryImageCreateInfo emi2 = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, .pNext = &eci, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    VkImageCreateInfo ici2 = ici; ici2.pNext = &emi2;
    VkImage ib; CK(vkCreateImage(b->dev, &ici2, NULL, &ib));
    VkMemoryRequirements mr2; vkGetImageMemoryRequirements(b->dev, ib, &mr2);
    VkMemoryFdPropertiesKHR fdp = { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
    CK(b->getMemoryFdProps(b->dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdp));
    fprintf(stderr, "importer: image req size %llu bits 0x%x, fd memoryTypeBits 0x%x\n", (unsigned long long)mr2.size, mr2.memoryTypeBits, fdp.memoryTypeBits);
    VkMemoryDedicatedAllocateInfo ded2 = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = ib };
    VkExportMemoryAllocateInfo exp2 = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, .pNext = &ded2, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    VkImportMemoryFdInfoKHR imp = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, .pNext = getenv("IMPORT_WITH_EXPORT") ? (void *)&exp2 : (void *)&ded2, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = fd };
    VkMemoryAllocateInfo mai2 = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &imp, .allocationSize = fdsize > 0 ? (VkDeviceSize)fdsize : mr2.size,
        .memoryTypeIndex = find_mem(b, mr2.memoryTypeBits & fdp.memoryTypeBits, 0) };
    VkDeviceMemory mb; CK(vkAllocateMemory(b->dev, &mai2, NULL, &mb));
    CK(vkBindImageMemory(b->dev, ib, mb, 0));

    // Readback buffer.
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    VkBuffer buf; CK(vkCreateBuffer(b->dev, &bci, NULL, &buf));
    VkMemoryRequirements bmr; vkGetBufferMemoryRequirements(b->dev, buf, &bmr);
    VkMemoryAllocateInfo bmai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = bmr.size,
        .memoryTypeIndex = find_mem(b, bmr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory bm; CK(vkAllocateMemory(b->dev, &bmai, NULL, &bm));
    CK(vkBindBufferMemory(b->dev, buf, bm, 0));
    uint32_t *px; CK(vkMapMemory(b->dev, bm, 0, W * H * 4, 0, (void **)&px));
    memset(px, 0x55, W * H * 4);

    cb = begin(b);
    barrier(cb, ib, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_FOREIGN_EXT, b->qf);
    VkBufferImageCopy cp = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
    vkCmdCopyImageToBuffer(cb, ib, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &cp);
    VkMemoryBarrier hb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
    submit_wait(b, cb);

    printf("readback: px[0]=%08x px[mid]=%08x px[last]=%08x\n", px[0], px[W * H / 2 + W / 2], px[W * H - 1]);
    int ok = px[0] == 0xffff0000 && px[W * H - 1] == 0xffff0000;

    // Reverse: the importer clears green, the exporter reads it back.
    cb = begin(b);
    barrier(cb, ib, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED);
    VkClearColorValue green = { .float32 = { 0, 1, 0, 1 } };
    vkCmdClearColorImage(cb, ib, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &green, 1, &rng);
    barrier(cb, ib, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, b->qf, VK_QUEUE_FAMILY_FOREIGN_EXT);
    submit_wait(b, cb);
    VkBuffer abuf; CK(vkCreateBuffer(a.dev, &bci, NULL, &abuf));
    VkMemoryRequirements amr; vkGetBufferMemoryRequirements(a.dev, abuf, &amr);
    VkMemoryAllocateInfo amai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = amr.size,
        .memoryTypeIndex = find_mem(&a, amr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory am; CK(vkAllocateMemory(a.dev, &amai, NULL, &am));
    CK(vkBindBufferMemory(a.dev, abuf, am, 0));
    uint32_t *apx; CK(vkMapMemory(a.dev, am, 0, W * H * 4, 0, (void **)&apx));
    memset(apx, 0x55, W * H * 4);
    cb = begin(&a);
    barrier(cb, ia, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_FOREIGN_EXT, a.qf);
    vkCmdCopyImageToBuffer(cb, ia, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, abuf, 1, &cp);
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
    submit_wait(&a, cb);
    int ok2 = apx[0] == 0xff00ff00 && apx[W * H - 1] == 0xff00ff00;
    printf("reverse readback: px[0]=%08x px[last]=%08x -> %s\n", apx[0], apx[W * H - 1], ok2 ? "PASS" : "FAIL");
    ok = ok && ok2;
    printf("%s (%s, modifier 0x%016llx)\n", ok ? "PASS" : "FAIL", cross ? "cross" : "same", (unsigned long long)mp.drmFormatModifier);
    return ok ? 0 : 2;
}
