/* Host check for nvtex: what vkGetPhysicalDeviceImageFormatProperties2
 * allows for the create-info the Yttrium winsys uses for a D3D9 texture in
 * a video-memory pool (a "shared colour" image): 2D, LINEAR, usage
 * TRANSFER_SRC|TRANSFER_DST|SAMPLED|COLOR_ATTACHMENT, flags
 * MUTABLE_FORMAT|ALIAS. Prints maxMipLevels and maxArrayLayers for LINEAR
 * and OPTIMAL, per format, for every physical device.
 *   cc -o linearmips linearmips.c -lvulkan
 *   VK_LOADER_DRIVERS_SELECT='*nvidia*' ./linearmips   (or '*radeon*')
 */
#include <stdio.h>
#include <vulkan/vulkan.h>

int main(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "linearmips", 1, NULL, 0, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) { puts("no instance"); return 1; }
    uint32_t n = 8;
    VkPhysicalDevice pds[8];
    vkEnumeratePhysicalDevices(inst, &n, pds);
    const struct { VkFormat f; const char *name; } fmts[] = {
        { VK_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM" },
        { VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM" },
        { VK_FORMAT_R5G6B5_UNORM_PACK16, "R5G6B5_UNORM_PACK16" },
        { VK_FORMAT_B5G6R5_UNORM_PACK16, "B5G6R5_UNORM_PACK16" },
        { VK_FORMAT_A1R5G5B5_UNORM_PACK16, "A1R5G5B5_UNORM_PACK16" },
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, "A2B10G10R10_UNORM_PACK32" },
        { VK_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM" },
    };
    for (uint32_t d = 0; d < n; d++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pds[d], &p);
        printf("device %s, driver version 0x%x\n", p.deviceName, p.driverVersion);
        for (unsigned i = 0; i < sizeof fmts / sizeof *fmts; i++) {
            for (int t = 0; t < 2; t++) {
                VkImageTiling tiling = t ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;
                VkPhysicalDeviceImageFormatInfo2 info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, NULL,
                    fmts[i].f, VK_IMAGE_TYPE_2D, tiling,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_ALIAS_BIT };
                VkImageFormatProperties2 props = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
                VkResult r = vkGetPhysicalDeviceImageFormatProperties2(pds[d], &info, &props);
                printf("  %-26s %-7s result %d maxMipLevels %u maxArrayLayers %u maxExtent %ux%u\n",
                       fmts[i].name, t ? "OPTIMAL" : "LINEAR", r,
                       props.imageFormatProperties.maxMipLevels, props.imageFormatProperties.maxArrayLayers,
                       props.imageFormatProperties.maxExtent.width, props.imageFormatProperties.maxExtent.height);
            }
        }
    }
    vkDestroyInstance(inst, NULL);
    return 0;
}
