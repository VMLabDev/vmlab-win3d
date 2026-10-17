/* Vulkan probe: loads the system vulkan-1.dll at run time (so a missing loader
 * is a clean error, not a failed process start), creates an instance with
 * VK_KHR_win32_surface, picks a device that can present to the window, renders
 * through a swap chain and reads the last frame back with a buffer copy. */
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include "../common/probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "vulkan_shaders.h"

#define MAX_IMAGES 8

/* Instance-level and device-level functions this probe calls. */
#define INSTANCE_FUNCS(X)                                                                          \
    X(vkDestroyInstance)                                                                           \
    X(vkEnumeratePhysicalDevices)                                                                  \
    X(vkGetPhysicalDeviceProperties)                                                               \
    X(vkGetPhysicalDeviceProperties2)                                                              \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                    \
    X(vkGetPhysicalDeviceMemoryProperties)                                                         \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                        \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                   \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                        \
    X(vkCreateWin32SurfaceKHR)                                                                     \
    X(vkDestroySurfaceKHR)                                                                         \
    X(vkCreateDevice)                                                                              \
    X(vkGetDeviceProcAddr)

#define DEVICE_FUNCS(X)                                                                            \
    X(vkDestroyDevice)                                                                             \
    X(vkGetDeviceQueue)                                                                            \
    X(vkDeviceWaitIdle)                                                                            \
    X(vkCreateSwapchainKHR)                                                                        \
    X(vkDestroySwapchainKHR)                                                                       \
    X(vkGetSwapchainImagesKHR)                                                                     \
    X(vkAcquireNextImageKHR)                                                                       \
    X(vkQueuePresentKHR)                                                                           \
    X(vkQueueSubmit)                                                                               \
    X(vkCreateImageView)                                                                           \
    X(vkDestroyImageView)                                                                          \
    X(vkCreateRenderPass)                                                                          \
    X(vkDestroyRenderPass)                                                                         \
    X(vkCreateFramebuffer)                                                                         \
    X(vkDestroyFramebuffer)                                                                        \
    X(vkCreateShaderModule)                                                                        \
    X(vkDestroyShaderModule)                                                                       \
    X(vkCreatePipelineLayout)                                                                      \
    X(vkDestroyPipelineLayout)                                                                     \
    X(vkCreateGraphicsPipelines)                                                                   \
    X(vkDestroyPipeline)                                                                           \
    X(vkCreateBuffer)                                                                              \
    X(vkDestroyBuffer)                                                                             \
    X(vkGetBufferMemoryRequirements)                                                               \
    X(vkAllocateMemory)                                                                            \
    X(vkFreeMemory)                                                                                \
    X(vkBindBufferMemory)                                                                          \
    X(vkMapMemory)                                                                                 \
    X(vkCreateCommandPool)                                                                         \
    X(vkDestroyCommandPool)                                                                        \
    X(vkAllocateCommandBuffers)                                                                    \
    X(vkBeginCommandBuffer)                                                                        \
    X(vkEndCommandBuffer)                                                                          \
    X(vkResetCommandBuffer)                                                                        \
    X(vkCmdBeginRenderPass)                                                                        \
    X(vkCmdEndRenderPass)                                                                          \
    X(vkCmdBindPipeline)                                                                           \
    X(vkCmdBindVertexBuffers)                                                                      \
    X(vkCmdSetViewport)                                                                            \
    X(vkCmdSetScissor)                                                                             \
    X(vkCmdDraw)                                                                                   \
    X(vkCmdPipelineBarrier)                                                                        \
    X(vkCmdCopyImageToBuffer)                                                                      \
    X(vkCreateFence)                                                                               \
    X(vkDestroyFence)                                                                              \
    X(vkWaitForFences)                                                                             \
    X(vkResetFences)                                                                               \
    X(vkCreateSemaphore)                                                                           \
    X(vkDestroySemaphore)

#define DECLARE(f) static PFN_##f f;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
static PFN_vkCreateInstance vkCreateInstance;
static PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
static PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
INSTANCE_FUNCS(DECLARE)
DEVICE_FUNCS(DECLARE)

static const char *vk_result(VkResult r)
{
    switch (r) {
#define C(x) case x: return #x;
    C(VK_SUCCESS) C(VK_NOT_READY) C(VK_TIMEOUT) C(VK_SUBOPTIMAL_KHR)
    C(VK_ERROR_OUT_OF_HOST_MEMORY) C(VK_ERROR_OUT_OF_DEVICE_MEMORY)
    C(VK_ERROR_INITIALIZATION_FAILED) C(VK_ERROR_DEVICE_LOST) C(VK_ERROR_MEMORY_MAP_FAILED)
    C(VK_ERROR_LAYER_NOT_PRESENT) C(VK_ERROR_EXTENSION_NOT_PRESENT)
    C(VK_ERROR_FEATURE_NOT_PRESENT) C(VK_ERROR_INCOMPATIBLE_DRIVER)
    C(VK_ERROR_SURFACE_LOST_KHR) C(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
    C(VK_ERROR_OUT_OF_DATE_KHR) C(VK_ERROR_UNKNOWN)
#undef C
    default: return "VkResult(other)";
    }
}

static int vk_failed(probe_ctx *ctx, VkResult r, const char *what)
{
    if (r == VK_SUCCESS)
        return 0;
    probe_error(ctx, "%s failed: %s (%d)", what, vk_result(r), (int)r);
    return 1;
}

static const char *device_type(VkPhysicalDeviceType t)
{
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

static const char *api_version(uint32_t v)
{
    static char b[32];
    snprintf(b, sizeof b, "%u.%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v),
             VK_API_VERSION_PATCH(v));
    return b;
}

static int memory_type(VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & want) == want)
            return (int)i;
    return -1;
}

typedef struct host_buffer {
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
} host_buffer;

static int make_buffer(probe_ctx *ctx, VkDevice dev, VkPhysicalDeviceMemoryProperties *mp,
                       VkDeviceSize size, VkBufferUsageFlags usage, host_buffer *out)
{
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vk_failed(ctx, vkCreateBuffer(dev, &bi, NULL, &out->buf), "vkCreateBuffer"))
        return 1;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, out->buf, &req);
    int type = memory_type(mp, req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type < 0) {
        probe_error(ctx, "no host-visible coherent memory type");
        return 1;
    }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    if (vk_failed(ctx, vkAllocateMemory(dev, &ai, NULL, &out->mem), "vkAllocateMemory") ||
        vk_failed(ctx, vkBindBufferMemory(dev, out->buf, out->mem, 0), "vkBindBufferMemory") ||
        vk_failed(ctx, vkMapMemory(dev, out->mem, 0, VK_WHOLE_SIZE, 0, &out->map), "vkMapMemory"))
        return 1;
    return 0;
}

static void image_barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

static int run(probe_ctx *ctx)
{
    HMODULE loader = LoadLibraryExW(L"vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) {
        probe_error(ctx, "Vulkan loader System32\\vulkan-1.dll not found (error %lu)", GetLastError());
        return 1;
    }
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(loader, path, MAX_PATH);
        probe_wstr(ctx, PROBE_API, "loader", path);
    }
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(loader, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) {
        probe_error(ctx, "vulkan-1.dll has no vkGetInstanceProcAddr");
        return 1;
    }
    vkCreateInstance = (PFN_vkCreateInstance)(void *)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)(void *)
        vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");
    vkEnumerateInstanceVersion =
        (PFN_vkEnumerateInstanceVersion)(void *)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion)
        vkEnumerateInstanceVersion(&loader_version);
    probe_str(ctx, PROBE_API, "instance_version", api_version(loader_version));

    uint32_t next = 0;
    int has_surface = 0, has_win32 = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &next, NULL);
    VkExtensionProperties *exts = (VkExtensionProperties *)calloc(next ? next : 1, sizeof *exts);
    vkEnumerateInstanceExtensionProperties(NULL, &next, exts);
    for (uint32_t i = 0; i < next; i++) {
        has_surface |= !strcmp(exts[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
        has_win32 |= !strcmp(exts[i].extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
    free(exts);
    if (!has_surface || !has_win32) {
        probe_error(ctx, "instance lacks %s (no Vulkan ICD with Win32 WSI is registered)",
                    has_surface ? VK_KHR_WIN32_SURFACE_EXTENSION_NAME : VK_KHR_SURFACE_EXTENSION_NAME);
        return 1;
    }

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "vmlab-win3d vulkan_probe";
    app.apiVersion = loader_version >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    const char *iext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iext;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ici, NULL, &inst);
    if (r == VK_ERROR_INCOMPATIBLE_DRIVER) {
        probe_error(ctx, "vkCreateInstance: VK_ERROR_INCOMPATIBLE_DRIVER (no usable Vulkan ICD)");
        return 1;
    }
    if (vk_failed(ctx, r, "vkCreateInstance"))
        return 1;
#define LOAD_I(f) f = (PFN_##f)(void *)vkGetInstanceProcAddr(inst, #f);
    INSTANCE_FUNCS(LOAD_I)
#undef LOAD_I

    VkWin32SurfaceCreateInfoKHR sci = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = GetModuleHandleW(NULL);
    sci.hwnd = ctx->hwnd;
    VkSurfaceKHR surface;
    if (vk_failed(ctx, vkCreateWin32SurfaceKHR(inst, &sci, NULL, &surface), "vkCreateWin32SurfaceKHR"))
        return 1;

    uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(inst, &ndev, NULL);
    probe_int(ctx, PROBE_API, "physical_device_count", ndev);
    if (ndev == 0) {
        probe_error(ctx, "vkEnumeratePhysicalDevices: no physical devices (no usable Vulkan ICD)");
        return 1;
    }
    VkPhysicalDevice *devs = (VkPhysicalDevice *)calloc(ndev, sizeof *devs);
    vkEnumeratePhysicalDevices(inst, &ndev, devs);

    /* Prefer a GPU over a CPU device; it must have a graphics queue that can
     * present to the window. */
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    uint32_t qfam = 0;
    int best = -1;
    for (uint32_t i = 0; i < ndev; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devs[i], &p);
        char desc[300];
        snprintf(desc, sizeof desc, "%s (%s, vendor 0x%04x, device 0x%04x, API %s)", p.deviceName,
                 device_type(p.deviceType), p.vendorID, p.deviceID, api_version(p.apiVersion));
        probe_list_add(ctx, PROBE_API, "physical_devices", desc);
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
        VkQueueFamilyProperties qp[16];
        if (nq > 16)
            nq = 16;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qp);
        for (uint32_t q = 0; q < nq; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, surface, &present);
            if ((qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : 1;
                if (score > best) {
                    best = score;
                    phys = devs[i];
                    qfam = q;
                }
                break;
            }
        }
    }
    free(devs);
    if (!phys) {
        probe_error(ctx, "no Vulkan device has a graphics queue that can present to the window");
        return 1;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);
    probe_str(ctx, PROBE_ADAPTER, "name", props.deviceName);
    const char *vendor = probe_vendor_name(props.vendorID);
    probe_str(ctx, PROBE_ADAPTER, "vendor", vendor ? vendor : "");
    probe_hex(ctx, PROBE_ADAPTER, "vendor_id", props.vendorID);
    probe_hex(ctx, PROBE_ADAPTER, "device_id", props.deviceID);
    probe_int(ctx, PROBE_ADAPTER, "driver_version_raw", props.driverVersion);
    probe_str(ctx, PROBE_API, "device_type", device_type(props.deviceType));
    probe_str(ctx, PROBE_API, "device_api_version", api_version(props.apiVersion));
    if (props.apiVersion >= VK_API_VERSION_1_2 && app.apiVersion >= VK_API_VERSION_1_1 &&
        vkGetPhysicalDeviceProperties2) {
        VkPhysicalDeviceDriverProperties drv = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &drv;
        vkGetPhysicalDeviceProperties2(phys, &p2);
        probe_str(ctx, PROBE_API, "driver_name", drv.driverName);
        probe_str(ctx, PROBE_API, "driver_info", drv.driverInfo);
        probe_int(ctx, PROBE_API, "driver_id", drv.driverID);
    }
    probe_classify(ctx, props.deviceName, (long)props.vendorID,
                   props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU, "VK_PHYSICAL_DEVICE_TYPE_CPU");
    char sub[300];
    snprintf(sub, sizeof sub, "%s - %s", ctx->arch, props.deviceName);
    probe_set_subtitle(ctx, sub);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char *dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dext;
    VkDevice dev;
    if (vk_failed(ctx, vkCreateDevice(phys, &dci, NULL, &dev), "vkCreateDevice"))
        return 1;
#define LOAD_D(f) f = (PFN_##f)(void *)vkGetDeviceProcAddr(dev, #f);
    DEVICE_FUNCS(LOAD_D)
#undef LOAD_D
    VkQueue queue;
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    /* Swap chain: an 8-bit UNORM format so the readback compares exactly. */
    VkSurfaceCapabilitiesKHR caps;
    if (vk_failed(ctx, vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps),
                  "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return 1;
    uint32_t nfmt = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nfmt, NULL);
    VkSurfaceFormatKHR fmts[64];
    if (nfmt > 64)
        nfmt = 64;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nfmt, fmts);
    VkSurfaceFormatKHR fmt = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (uint32_t i = 0; i < nfmt; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            fmt = fmts[i];
            break;
        }
    if (fmt.format == VK_FORMAT_UNDEFINED) {
        probe_error(ctx, "surface offers no B8G8R8A8_UNORM or R8G8B8A8_UNORM format");
        return 1;
    }
    int bgra = fmt.format == VK_FORMAT_B8G8R8A8_UNORM;
    probe_str(ctx, PROBE_API, "swapchain_format", bgra ? "B8G8R8A8_UNORM" : "R8G8B8A8_UNORM");
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xffffffffu) {
        extent.width = (uint32_t)ctx->width;
        extent.height = (uint32_t)ctx->height;
    }
    if ((int)extent.width != ctx->width || (int)extent.height != ctx->height)
        probe_error(ctx, "surface extent %ux%u differs from the window %dx%d", extent.width,
                    extent.height, ctx->width, ctx->height);
    int can_read = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (!can_read)
        probe_error(ctx, "swap chain images do not support TRANSFER_SRC; cannot read back");
    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount)
        want = caps.maxImageCount;
    VkSwapchainCreateInfoKHR swci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swci.surface = surface;
    swci.minImageCount = want;
    swci.imageFormat = fmt.format;
    swci.imageColorSpace = fmt.colorSpace;
    swci.imageExtent = extent;
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (can_read ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                              ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                              : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha &
                                                              -caps.supportedCompositeAlpha);
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped = VK_TRUE;
    VkSwapchainKHR swap;
    if (vk_failed(ctx, vkCreateSwapchainKHR(dev, &swci, NULL, &swap), "vkCreateSwapchainKHR"))
        return 1;
    uint32_t nimg = 0;
    vkGetSwapchainImagesKHR(dev, swap, &nimg, NULL);
    if (nimg > MAX_IMAGES)
        nimg = MAX_IMAGES;
    VkImage images[MAX_IMAGES];
    vkGetSwapchainImagesKHR(dev, swap, &nimg, images);
    probe_int(ctx, PROBE_API, "swapchain_images", nimg);

    VkAttachmentDescription att = {0};
    att.format = fmt.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub_desc = {0};
    sub_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub_desc.colorAttachmentCount = 1;
    sub_desc.pColorAttachments = &ref;
    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub_desc;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    VkRenderPass rp;
    if (vk_failed(ctx, vkCreateRenderPass(dev, &rpi, NULL, &rp), "vkCreateRenderPass"))
        return 1;

    VkImageView views[MAX_IMAGES];
    VkFramebuffer fbs[MAX_IMAGES];
    VkSemaphore done_sem[MAX_IMAGES];
    for (uint32_t i = 0; i < nimg; i++) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = fmt.format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vk_failed(ctx, vkCreateImageView(dev, &vi, NULL, &views[i]), "vkCreateImageView"))
            return 1;
        VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = rp;
        fi.attachmentCount = 1;
        fi.pAttachments = &views[i];
        fi.width = extent.width;
        fi.height = extent.height;
        fi.layers = 1;
        if (vk_failed(ctx, vkCreateFramebuffer(dev, &fi, NULL, &fbs[i]), "vkCreateFramebuffer"))
            return 1;
        VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vk_failed(ctx, vkCreateSemaphore(dev, &si, NULL, &done_sem[i]), "vkCreateSemaphore"))
            return 1;
    }

    VkShaderModuleCreateInfo smi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vsm, fsm;
    smi.codeSize = sizeof probe_vert_spv;
    smi.pCode = probe_vert_spv;
    if (vk_failed(ctx, vkCreateShaderModule(dev, &smi, NULL, &vsm), "vkCreateShaderModule(vert)"))
        return 1;
    smi.codeSize = sizeof probe_frag_spv;
    smi.pCode = probe_frag_spv;
    if (vk_failed(ctx, vkCreateShaderModule(dev, &smi, NULL, &fsm), "vkCreateShaderModule(frag)"))
        return 1;

    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    if (vk_failed(ctx, vkCreatePipelineLayout(dev, &pli, NULL, &layout), "vkCreatePipelineLayout"))
        return 1;
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vsm, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fsm, "main", NULL},
    };
    VkVertexInputBindingDescription vbind = {0, sizeof(probe_vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vattr[2] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8},
    };
    VkPipelineVertexInputStateCreateInfo vis = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &vbind;
    vis.vertexAttributeDescriptionCount = 2;
    vis.pVertexAttributeDescriptions = vattr;
    VkPipelineInputAssemblyStateCreateInfo ias = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo ras = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    ras.polygonMode = VK_POLYGON_MODE_FILL;
    ras.cullMode = VK_CULL_MODE_NONE;
    ras.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    ras.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo mss = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    mss.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = 1;
    cbs.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dys = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dys.dynamicStateCount = 2;
    dys.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gpi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vis;
    gpi.pInputAssemblyState = &ias;
    gpi.pViewportState = &vps;
    gpi.pRasterizationState = &ras;
    gpi.pMultisampleState = &mss;
    gpi.pColorBlendState = &cbs;
    gpi.pDynamicState = &dys;
    gpi.layout = layout;
    gpi.renderPass = rp;
    VkPipeline pipe;
    if (vk_failed(ctx, vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &pipe),
                  "vkCreateGraphicsPipelines"))
        return 1;

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    host_buffer vb, rb;
    VkDeviceSize rb_bytes = (VkDeviceSize)extent.width * extent.height * 4;
    if (make_buffer(ctx, dev, &mp, PROBE_MAX_VERTICES * sizeof(probe_vertex),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vb) ||
        make_buffer(ctx, dev, &mp, rb_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &rb))
        return 1;

    VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = qfam;
    VkCommandPool pool;
    if (vk_failed(ctx, vkCreateCommandPool(dev, &cpi, NULL, &pool), "vkCreateCommandPool"))
        return 1;
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    if (vk_failed(ctx, vkAllocateCommandBuffers(dev, &cai, &cb), "vkAllocateCommandBuffers"))
        return 1;
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore acquire_sem;
    if (vk_failed(ctx, vkCreateFence(dev, &fci, NULL, &fence), "vkCreateFence") ||
        vk_failed(ctx, vkCreateSemaphore(dev, &si, NULL, &acquire_sem), "vkCreateSemaphore"))
        return 1;

    VkViewport viewport = {0, 0, (float)extent.width, (float)extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, extent};
    for (;;) {
        probe_pump(ctx);
        int last = probe_last_frame(ctx);
        /* One frame in flight, so the vertex buffer is free once the fence is. */
        if (vk_failed(ctx, vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull), "vkWaitForFences"))
            return 1;
        uint32_t idx;
        r = vkAcquireNextImageKHR(dev, swap, 10000000000ull, acquire_sem, VK_NULL_HANDLE, &idx);
        if (r != VK_SUBOPTIMAL_KHR && vk_failed(ctx, r, "vkAcquireNextImageKHR"))
            return 1;
        vkResetFences(dev, 1, &fence);
        int n = probe_build_scene(ctx, probe_now(ctx), (probe_vertex *)vb.map, PROBE_MAX_VERTICES);

        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkClearValue clear;
        memcpy(clear.color.float32, probe_clear_color, sizeof clear.color.float32);
        VkRenderPassBeginInfo rbi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = rp;
        rbi.framebuffer = fbs[idx];
        rbi.renderArea.extent = extent;
        rbi.clearValueCount = 1;
        rbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vb.buf, &zero);
        vkCmdDraw(cb, (uint32_t)n, 1, 0, 0);
        vkCmdEndRenderPass(cb);
        if (last && can_read) {
            image_barrier(cb, images[idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy = {0};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent.width = extent.width;
            copy.imageExtent.height = extent.height;
            copy.imageExtent.depth = 1;
            vkCmdCopyImageToBuffer(cb, images[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buf, 1, &copy);
            image_barrier(cb, images[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        }
        if (vk_failed(ctx, vkEndCommandBuffer(cb), "vkEndCommandBuffer"))
            return 1;

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo sub = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &acquire_sem;
        sub.pWaitDstStageMask = &wait_stage;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &cb;
        sub.signalSemaphoreCount = 1;
        sub.pSignalSemaphores = &done_sem[idx];
        if (vk_failed(ctx, vkQueueSubmit(queue, 1, &sub, fence), "vkQueueSubmit"))
            return 1;
        VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &done_sem[idx];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap;
        pi.pImageIndices = &idx;
        r = vkQueuePresentKHR(queue, &pi);
        if (r != VK_SUBOPTIMAL_KHR && vk_failed(ctx, r, "vkQueuePresentKHR"))
            return 1;
        probe_frame_presented(ctx);
        if (last) {
            if (vk_failed(ctx, vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull),
                          "vkWaitForFences(readback)"))
                return 1;
            if (can_read)
                probe_readback_image(ctx, (const uint8_t *)rb.map, (size_t)extent.width * 4, bgra, 0);
            break;
        }
    }
    vkDeviceWaitIdle(dev);
    vkDestroySemaphore(dev, acquire_sem, NULL);
    vkDestroyFence(dev, fence, NULL);
    vkDestroyCommandPool(dev, pool, NULL);
    vkDestroyBuffer(dev, vb.buf, NULL);
    vkFreeMemory(dev, vb.mem, NULL);
    vkDestroyBuffer(dev, rb.buf, NULL);
    vkFreeMemory(dev, rb.mem, NULL);
    vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyPipelineLayout(dev, layout, NULL);
    vkDestroyShaderModule(dev, vsm, NULL);
    vkDestroyShaderModule(dev, fsm, NULL);
    for (uint32_t i = 0; i < nimg; i++) {
        vkDestroySemaphore(dev, done_sem[i], NULL);
        vkDestroyFramebuffer(dev, fbs[i], NULL);
        vkDestroyImageView(dev, views[i], NULL);
    }
    vkDestroyRenderPass(dev, rp, NULL);
    vkDestroySwapchainKHR(dev, swap, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroySurfaceKHR(inst, surface, NULL);
    vkDestroyInstance(inst, NULL);
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    probe_ctx ctx;
    int rc = probe_init(&ctx, argc, argv, "vulkan", "Vulkan", "VULKAN", 0);
    if (rc)
        return rc;
    if (ctx.hwnd)
        run(&ctx);
    return probe_finish(&ctx);
}
