/* Host-only Vulkan repro for Godot's Xid 69, ErrorCode 0x13d (no VM, no Venus).
 *
 *   linblend <color tiling: linear|optimal> <blend: none|alpha> [quads] [device index] [mem: sysmem|vram|import] [cover: small|full] [vrs: none|feature|WxH]
 *
 * Godot's last draw of a frame blits its render target onto the swapchain
 * image with alpha blending: one quad, 6 indices (the Data in the Xid), into
 * a 1280x720 B8G8R8A8_UNORM colour attachment, no depth. The Venus Win32 WSI
 * makes swapchain images LINEAR. This program does the same draw into an
 * image of the given tiling, with blending off or SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
 * waits for the queue and reads one pixel (sysmem only). mem places a LINEAR
 * image in host-visible system memory or in device-local VRAM (Godot's
 * swapchain images are imported device-local memory); cover full draws one
 * quad over the whole target, as Godot's blit does. mem import reproduces the
 * Venus Win32 swapchain: a second VkDevice (the D3D11 UMD's context) creates
 * the image (MUTABLE|ALIAS, usage 0x17) in exportable device-local memory,
 * exports it as a dma-buf, and the drawing device imports it as a dedicated
 * allocation for its own LINEAR ALIAS image, as vkr does for a guest blob.
 * vrs feature only enables the device feature; vrs WxH (Godot: 4x4) enables VK_KHR_fragment_shading_rate and gives the pipeline the
 * state every Godot pipeline carries when the device has the extension:
 * VkPipelineFragmentShadingRateStateCreateInfoKHR with fragmentSize WxH and
 * combiner ops KEEP, REPLACE, with no shading-rate attachment in the pass. Adapted from
 * docs/evidence/xid69/host-tests/lindepth.c (LINEAR colour with depth).
 * Build: build.sh.
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linblend_spv.h" /* vs_spv, fs_spv: glslc output, see build.sh */

#define W 1280
#define H 720
static int QUADS = 1;
#define CHECK(x) do { VkResult r_ = (x); if (r_) { printf("FAIL %s = %d\n", #x, r_); exit(1); } } while (0)

static VkDevice dev;
static VkPhysicalDeviceMemoryProperties memprops;

static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < memprops.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want)
            return i;
    printf("FAIL no memory type for bits 0x%x flags 0x%x\n", bits, want);
    exit(1);
}

static VkDeviceMemory bind_image(VkImage img, VkMemoryPropertyFlags want)
{
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, memtype(mr.memoryTypeBits, want) };
    VkDeviceMemory m;
    CHECK(vkAllocateMemory(dev, &ai, NULL, &m));
    CHECK(vkBindImageMemory(dev, img, m, 0));
    return m;
}

int main(int argc, char **argv)
{
    const char *ctiling = argc > 1 ? argv[1] : "linear";
    const char *blend = argc > 2 ? argv[2] : "alpha";
    const int blending = strcmp(blend, "none") != 0;
    const char *depth = "none";
    const int indexed = 1;
    if (argc > 3) QUADS = atoi(argv[3]);
    const int devidx = argc > 4 ? atoi(argv[4]) : 0;
    const int import = argc > 5 && !strcmp(argv[5], "import");
    const int vram = import || (argc > 5 && !strcmp(argv[5], "vram"));
    const int full = argc > 6 && !strcmp(argv[6], "full");
    uint32_t vrs_w = 0, vrs_h = 0;
    if (argc > 7 && sscanf(argv[7], "%ux%u", &vrs_w, &vrs_h) != 2)
        vrs_w = vrs_h = 0;
    const int vrs_feature_only = argc > 7 && !strcmp(argv[7], "feature");
    const int vrs = vrs_w != 0 || vrs_feature_only;
    const VkImageTiling tiling = strcmp(ctiling, "optimal") ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    VkFormat zfmt = VK_FORMAT_UNDEFINED;
    if (!strcmp(depth, "d16")) zfmt = VK_FORMAT_D16_UNORM;
    else if (!strcmp(depth, "d32")) zfmt = VK_FORMAT_D32_SFLOAT;
    else if (!strcmp(depth, "x8d24")) zfmt = VK_FORMAT_X8_D24_UNORM_PACK32;
    const VkFormat cfmt = VK_FORMAT_B8G8R8A8_UNORM;

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "lindepth", 0, NULL, 0, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 8;
    VkPhysicalDevice pds[8];
    vkEnumeratePhysicalDevices(inst, &n, pds);
    if ((uint32_t)devidx >= n) { printf("FAIL no device %d\n", devidx); return 1; }
    VkPhysicalDevice pd = pds[devidx];
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(pd, &p);
    vkGetPhysicalDeviceMemoryProperties(pd, &memprops);
    VkPhysicalDevicePCIBusInfoPropertiesEXT pci = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &pci };
    vkGetPhysicalDeviceProperties2(pd, &p2);
    printf("mem %s, cover %s, vrs %s\n", import ? "import" : vram ? "vram" : "sysmem", full ? "full" : "small", argc > 7 ? argv[7] : "none");
    printf("device %d %s (pci %04x:%02x:%02x) driver 0x%x; colour %s, blend %s, vkCmdDrawIndexed %d\n", devidx, p.deviceName,
           pci.pciDomain, pci.pciBus, pci.pciDevice, p.driverVersion, ctiling, blend, QUADS * 6);
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(pd, cfmt, &fp);
    printf("B8G8R8A8_UNORM linearTilingFeatures 0x%x (COLOR_ATTACHMENT %s)\n", fp.linearTilingFeatures,
           (fp.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ? "yes" : "no");
    if (zfmt) {
        vkGetPhysicalDeviceFormatProperties(pd, zfmt, &fp);
        printf("depth format %d optimalTilingFeatures 0x%x\n", zfmt, fp.optimalTilingFeatures);
        if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) { printf("SKIP depth format unsupported\n"); return 2; }
    }
    VkImageFormatProperties ifp;
    VkResult r = vkGetPhysicalDeviceImageFormatProperties(pd, cfmt, VK_IMAGE_TYPE_2D, tiling,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0, &ifp);
    printf("colour image format properties: %d (maxExtent %ux%u)\n", r, ifp.maxExtent.width, ifp.maxExtent.height);
    if (r) return 2;

    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    const char *exts[3];
    uint32_t next = 0;
    if (import) {
        exts[next++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
        exts[next++] = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
    }
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR fsr = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
    if (vrs) {
        VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &fsr };
        vkGetPhysicalDeviceFeatures2(pd, &f2);
        printf("fragment shading rate: pipeline %u primitive %u attachment %u\n", fsr.pipelineFragmentShadingRate,
               fsr.primitiveFragmentShadingRate, fsr.attachmentFragmentShadingRate);
        exts[next++] = VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
    }
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, vrs ? &fsr : NULL, 0, 1, &qci, 0, NULL, next, exts };
    int fd = -1;
    VkDeviceSize export_size = 0;
    if (import) {
        /* The exporting device: Yttrium's D3D11 shared texture. */
        VkDevice exp;
        CHECK(vkCreateDevice(pd, &dci, NULL, &exp));
        VkImageCreateInfo eci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL,
                                  VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_ALIAS_BIT, VK_IMAGE_TYPE_2D, cfmt,
                                  { W, H, 1 }, 1, 1, VK_SAMPLE_COUNT_1_BIT, tiling, 0x17, VK_SHARING_MODE_EXCLUSIVE, 0, NULL,
                                  VK_IMAGE_LAYOUT_UNDEFINED };
        VkImage eimg;
        CHECK(vkCreateImage(exp, &eci, NULL, &eimg));
        VkMemoryRequirements emr;
        vkGetImageMemoryRequirements(exp, eimg, &emr);
        VkExportMemoryAllocateInfo exi = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, NULL,
                                           VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
        VkMemoryAllocateInfo eai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exi, emr.size,
                                     memtype(emr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        VkDeviceMemory emem;
        CHECK(vkAllocateMemory(exp, &eai, NULL, &emem));
        CHECK(vkBindImageMemory(exp, eimg, emem, 0));
        PFN_vkGetMemoryFdKHR getfd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(exp, "vkGetMemoryFdKHR");
        VkMemoryGetFdInfoKHR gfi = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, NULL, emem,
                                     VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
        CHECK(getfd(exp, &gfi, &fd));
        export_size = emr.size;
        printf("exported %llu bytes as dma-buf fd %d (memory type %u)\n", (unsigned long long)emr.size, fd, eai.memoryTypeIndex);
    }
    CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
    VkQueue q;
    vkGetDeviceQueue(dev, 0, 0, &q);

    /* Colour attachment, host-visible when linear so the result can be read. */
    VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, import ? VK_IMAGE_CREATE_ALIAS_BIT : 0, VK_IMAGE_TYPE_2D, cfmt,
                             { W, H, 1 }, 1, 1, VK_SAMPLE_COUNT_1_BIT, tiling,
                             import ? 0x17 : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
    VkImage cimg;
    CHECK(vkCreateImage(dev, &ci, NULL, &cimg));
    VkDeviceMemory cmem;
    if (import) {
        VkMemoryRequirements imr;
        vkGetImageMemoryRequirements(dev, cimg, &imr);
        VkMemoryDedicatedAllocateInfo ded = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, NULL, cimg, VK_NULL_HANDLE };
        VkImportMemoryFdInfoKHR imp = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, &ded,
                                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd };
        VkMemoryAllocateInfo iai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp, export_size,
                                     memtype(imr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        printf("import: image needs %llu bytes, bits 0x%x; importing into type %u\n",
               (unsigned long long)imr.size, imr.memoryTypeBits, iai.memoryTypeIndex);
        CHECK(vkAllocateMemory(dev, &iai, NULL, &cmem));
        CHECK(vkBindImageMemory(dev, cimg, cmem, 0));
    } else {
        cmem = bind_image(cimg, tiling == VK_IMAGE_TILING_LINEAR && !vram ?
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, cimg, VK_IMAGE_VIEW_TYPE_2D, cfmt, {0},
                                  { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView views[2];
    CHECK(vkCreateImageView(dev, &vci, NULL, &views[0]));
    VkImage zimg = VK_NULL_HANDLE;
    if (zfmt) {
        VkImageCreateInfo zci = ci;
        zci.format = zfmt;
        zci.tiling = VK_IMAGE_TILING_OPTIMAL;
        zci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        CHECK(vkCreateImage(dev, &zci, NULL, &zimg));
        bind_image(zimg, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkImageViewCreateInfo zv = vci;
        zv.image = zimg;
        zv.format = zfmt;
        zv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        CHECK(vkCreateImageView(dev, &zv, NULL, &views[1]));
    }

    VkAttachmentDescription att[2] = {
        { 0, cfmt, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL },
        { 0, zfmt, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
    };
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference zref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, NULL, 1, &cref, NULL, zfmt ? &zref : NULL };
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, zfmt ? 2u : 1u, att, 1, &sub };
    VkRenderPass rp;
    CHECK(vkCreateRenderPass(dev, &rpci, NULL, &rp));
    VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, rp, zfmt ? 2u : 1u, views, W, H, 1 };
    VkFramebuffer fb;
    CHECK(vkCreateFramebuffer(dev, &fci, NULL, &fb));

    VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof vs_spv, vs_spv };
    VkShaderModule vs, fs;
    CHECK(vkCreateShaderModule(dev, &smi, NULL, &vs));
    smi.codeSize = sizeof fs_spv;
    smi.pCode = fs_spv;
    CHECK(vkCreateShaderModule(dev, &smi, NULL, &fs));
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 4 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 1, &pcr };
    VkPipelineLayout pl;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main" },
    };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
                                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport vp = { 0, 0, W, H, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, &vp, 1, &sc };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT };
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0,
                                                 VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };
    VkPipelineColorBlendAttachmentState cba = { 0 };
    cba.colorWriteMask = 0xf;
    if (blending) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, 1, &cba };
    VkPipelineFragmentShadingRateStateCreateInfoKHR fsrs = { VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR,
        NULL, { vrs_w, vrs_h }, { VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR } };
    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, vrs_w ? &fsrs : NULL, 0, 2, stages, &vi, &ia, NULL,
                                          &vps, &rs, &ms, zfmt ? &ds : NULL, &cb, NULL, pl, rp, 0 };
    VkPipeline pipe;
    CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe));

    /* Index buffer: 28 quads, 6 indices each. */
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, (VkDeviceSize)QUADS * 6 * 2, VK_BUFFER_USAGE_INDEX_BUFFER_BIT };
    VkBuffer ib;
    CHECK(vkCreateBuffer(dev, &bci, NULL, &ib));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, ib, &mr);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
                                 memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory ibm;
    CHECK(vkAllocateMemory(dev, &mai, NULL, &ibm));
    CHECK(vkBindBufferMemory(dev, ib, ibm, 0));
    uint16_t *ix;
    CHECK(vkMapMemory(dev, ibm, 0, VK_WHOLE_SIZE, 0, (void **)&ix));
    for (int qd = 0; qd < QUADS; qd++) {
        uint16_t b = (uint16_t)(qd * 4);
        uint16_t v[6] = { b, (uint16_t)(b + 1), (uint16_t)(b + 2), (uint16_t)(b + 2), (uint16_t)(b + 1), (uint16_t)(b + 3) };
        memcpy(ix + qd * 6, v, sizeof v);
    }

    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0 };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    VkClearValue clears[2];
    clears[0].color = (VkClearColorValue){ { 0.1f, 0.1f, 0.2f, 1 } };
    clears[1].depthStencil = (VkClearDepthStencilValue){ 1, 0 };
    VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, rp, fb, { { 0, 0 }, { W, H } }, zfmt ? 2u : 1u, clears };
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    const float scale = full ? 1.0f : 0.0f;
    vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_VERTEX_BIT, 0, 4, &scale);
    if (indexed) {
        vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, QUADS * 6, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, QUADS * 6, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
    CHECK(vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fci2 = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHECK(vkCreateFence(dev, &fci2, NULL, &fence));
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    CHECK(vkQueueSubmit(q, 1, &si, fence));
    r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
    printf("vkWaitForFences: %d%s\n", r, r == VK_ERROR_DEVICE_LOST ? " (DEVICE_LOST)" : r == VK_TIMEOUT ? " (TIMEOUT)" : "");
    if (r == VK_SUCCESS && tiling == VK_IMAGE_TILING_LINEAR && !vram) {
        VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        VkSubresourceLayout lay;
        vkGetImageSubresourceLayout(dev, cimg, &isr, &lay);
        uint8_t *px;
        CHECK(vkMapMemory(dev, cmem, 0, VK_WHOLE_SIZE, 0, (void **)&px));
        uint32_t c = *(uint32_t *)(px + lay.offset + 48 * lay.rowPitch + 48 * 4);
        uint32_t bg = *(uint32_t *)(px + lay.offset + 5 * lay.rowPitch + 5 * 4);
        printf("pixel in quad 0: 0x%08x, background: 0x%08x\n", c, bg);
    }
    printf("%s\n", r == VK_SUCCESS ? "OK" : "FAILED");
    return r == VK_SUCCESS ? 0 : 1;
}
