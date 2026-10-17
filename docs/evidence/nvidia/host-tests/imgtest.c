#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
static void try(VkPhysicalDevice pd, VkDevice dev, VkFormat fmt, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, unsigned w, unsigned h) {
  VkPhysicalDeviceImageFormatInfo2 fi = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, NULL, fmt, VK_IMAGE_TYPE_2D, tiling, usage, flags};
  VkHostImageCopyDevicePerformanceQuery hq = {VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY};
  VkImageFormatProperties2 fp = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, (usage & 0x400000) ? &hq : NULL};
  VkResult r1 = vkGetPhysicalDeviceImageFormatProperties2(pd, &fi, &fp);
  VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, flags, VK_IMAGE_TYPE_2D, fmt, {w,h,1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, tiling, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
  VkImage img; VkResult r2 = vkCreateImage(dev, &ci, NULL, &img);
  if (r2 == VK_SUCCESS) vkDestroyImage(dev, img, NULL);
  printf("fmt %d tiling %d usage 0x%x flags 0x%x %ux%u: formatprops %d (maxExtent %u) createImage %d\n", fmt, tiling, usage, flags, w, h, r1, fp.imageFormatProperties.maxExtent.width, r2);
}
int main(void) {
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "imgtest", 0, NULL, 0, VK_API_VERSION_1_3};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai};
  VkInstance inst; if (vkCreateInstance(&ici, NULL, &inst)) return 1;
  uint32_t n = 8; VkPhysicalDevice pds[8]; vkEnumeratePhysicalDevices(inst, &n, pds);
  VkPhysicalDevice pd = pds[0];
  VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd, &p);
  printf("device %s driver 0x%x api %u.%u\n", p.deviceName, p.driverVersion, VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion));
  VkFormatProperties3 f3 = {VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
  VkFormatProperties2 f2 = {VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &f3};
  VkFormat fmts[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
  for (int i = 0; i < 3; i++) { vkGetPhysicalDeviceFormatProperties2(pd, fmts[i], &f2);
    printf("fmt %d linear 0x%llx optimal 0x%llx\n", fmts[i], (unsigned long long)f3.linearTilingFeatures, (unsigned long long)f3.optimalTilingFeatures); }
  VkPhysicalDeviceHostImageCopyFeatures hf = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES, NULL, VK_TRUE};
  const char *ext[] = {"VK_EXT_host_image_copy"};
  float pr = 1; VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &pr};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &hf, 0, 1, &qci, 0, NULL, 1, ext};
  VkDevice dev; VkResult r = vkCreateDevice(pd, &dci, NULL, &dev); printf("createDevice %d\n", r); if (r) return 1;
  const VkImageUsageFlags U = 0x80017, Unh = 0x17;
  try(pd, dev, 44, VK_IMAGE_TILING_LINEAR, U, 0x8, 1280, 800);
  try(pd, dev, 9, VK_IMAGE_TILING_LINEAR, U, 0x8, 32, 32);
  try(pd, dev, 44, VK_IMAGE_TILING_OPTIMAL, U, 0x8, 16, 16);
  try(pd, dev, 44, VK_IMAGE_TILING_LINEAR, Unh, 0x8, 1280, 800);
  try(pd, dev, 44, VK_IMAGE_TILING_LINEAR, U, 0, 1280, 800);
  try(pd, dev, 44, VK_IMAGE_TILING_LINEAR, U & ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0x8, 1280, 800);
  try(pd, dev, 9, VK_IMAGE_TILING_LINEAR, Unh, 0x8, 32, 32);
  try(pd, dev, 9, VK_IMAGE_TILING_LINEAR, U, 0, 32, 32);
  try(pd, dev, 44, VK_IMAGE_TILING_LINEAR, U, 0x408, 1280, 800);
  try(pd, dev, 44, VK_IMAGE_TILING_LINEAR, Unh, 0x408, 1280, 800);
  return 0;
}
