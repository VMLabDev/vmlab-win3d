/* Diagnostic Vulkan layer (n5): create fences without SYNC_FD export, to test
 * whether NVIDIA's slow wake-up on exportable fences is what delays
 * virglrenderer's vkr queue-sync fences. vkGetFenceFdKHR on such a fence
 * resets it and returns fd -1 (the only caller in vkr closes the fd). */
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static PFN_vkGetInstanceProcAddr next_gipa;
static PFN_vkGetDeviceProcAddr next_gdpa;
static PFN_vkCreateFence next_create_fence;
static PFN_vkGetFenceFdKHR next_get_fence_fd;
static PFN_vkResetFences next_reset_fences;
static int stripped;

static VKAPI_ATTR VkResult VKAPI_CALL n5_CreateFence(VkDevice dev, const VkFenceCreateInfo *ci, const VkAllocationCallbacks *a, VkFence *f) {
   VkFenceCreateInfo copy = *ci;
   const VkBaseInStructure *p = ci->pNext;
   if (p && p->sType == VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO && !p->pNext) {
      copy.pNext = NULL;
      if (!stripped++) fprintf(stderr, "n5-layer: creating fences without SYNC_FD export\n");
   }
   return next_create_fence(dev, &copy, a, f);
}
static VKAPI_ATTR VkResult VKAPI_CALL n5_GetFenceFdKHR(VkDevice dev, const VkFenceGetFdInfoKHR *info, int *fd) {
   *fd = -1;
   return next_reset_fences(dev, 1, &info->fence);
}
static VKAPI_ATTR VkResult VKAPI_CALL n5_CreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a, VkInstance *inst) {
   VkLayerInstanceCreateInfo *li = (VkLayerInstanceCreateInfo *)ci->pNext;
   while (li && !(li->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && li->function == VK_LAYER_LINK_INFO))
      li = (VkLayerInstanceCreateInfo *)li->pNext;
   if (!li) return VK_ERROR_INITIALIZATION_FAILED;
   next_gipa = li->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   li->u.pLayerInfo = li->u.pLayerInfo->pNext;
   PFN_vkCreateInstance create = (PFN_vkCreateInstance)next_gipa(NULL, "vkCreateInstance");
   return create(ci, a, inst);
}
static VKAPI_ATTR VkResult VKAPI_CALL n5_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *a, VkDevice *dev) {
   VkLayerDeviceCreateInfo *li = (VkLayerDeviceCreateInfo *)ci->pNext;
   while (li && !(li->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && li->function == VK_LAYER_LINK_INFO))
      li = (VkLayerDeviceCreateInfo *)li->pNext;
   if (!li) return VK_ERROR_INITIALIZATION_FAILED;
   PFN_vkGetInstanceProcAddr gipa = li->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   next_gdpa = li->u.pLayerInfo->pfnNextGetDeviceProcAddr;
   li->u.pLayerInfo = li->u.pLayerInfo->pNext;
   PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(NULL, "vkCreateDevice");
   VkResult r = create(pd, ci, a, dev);
   if (r == VK_SUCCESS) {
      next_create_fence = (PFN_vkCreateFence)next_gdpa(*dev, "vkCreateFence");
      next_get_fence_fd = (PFN_vkGetFenceFdKHR)next_gdpa(*dev, "vkGetFenceFdKHR");
      next_reset_fences = (PFN_vkResetFences)next_gdpa(*dev, "vkResetFences");
   }
   return r;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL n5_GetDeviceProcAddr(VkDevice dev, const char *name) {
   if (!strcmp(name, "vkCreateFence")) return (PFN_vkVoidFunction)n5_CreateFence;
   if (!strcmp(name, "vkGetFenceFdKHR")) return (PFN_vkVoidFunction)n5_GetFenceFdKHR;
   if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)n5_GetDeviceProcAddr;
   return next_gdpa(dev, name);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL n5_GetInstanceProcAddr(VkInstance inst, const char *name) {
   if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)n5_CreateInstance;
   if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)n5_CreateDevice;
   if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)n5_GetInstanceProcAddr;
   if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)n5_GetDeviceProcAddr;
   if (!strcmp(name, "vkCreateFence")) return (PFN_vkVoidFunction)n5_CreateFence;
   if (!strcmp(name, "vkGetFenceFdKHR")) return (PFN_vkVoidFunction)n5_GetFenceFdKHR;
   return next_gipa(inst, name);
}
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
   if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
   v->pfnGetInstanceProcAddr = n5_GetInstanceProcAddr;
   v->pfnGetDeviceProcAddr = n5_GetDeviceProcAddr;
   v->pfnGetPhysicalDeviceProcAddr = NULL;
   return VK_SUCCESS;
}
