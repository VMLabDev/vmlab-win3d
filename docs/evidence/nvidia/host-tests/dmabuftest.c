/* Export a host-visible Vulkan allocation (as vkr does for blob scanouts) as a
 * dma-buf and import it into EGL on a GBM render node (as QEMU egl-headless
 * does), then read the pixels back. Usage: dmabuftest /dev/dri/renderDxxx [memtype] */
#include <vulkan/vulkan.h>
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 1280
#define H 800
int main(int argc, char **argv) {
  const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
  int want_type = argc > 2 ? atoi(argv[2]) : -1;
  const char *iexts[] = {"VK_KHR_external_memory_capabilities"};
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "dmabuftest", 0, NULL, 0, VK_API_VERSION_1_3};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 0, NULL};
  VkInstance inst; if (vkCreateInstance(&ici, NULL, &inst)) return 1;
  uint32_t n = 8; VkPhysicalDevice pds[8]; vkEnumeratePhysicalDevices(inst, &n, pds);
  VkPhysicalDevice pd = pds[0];
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  const char *dexts[] = {"VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf"};
  float pr = 1; VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &pr};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 2, dexts};
  VkDevice dev; if (vkCreateDevice(pd, &dci, NULL, &dev)) return 2;
  VkImageCreateInfo imci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM, {W,H,1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_LINEAR, 0x17, 0, 0, NULL, 0};
  VkImage img; if (vkCreateImage(dev, &imci, NULL, &img)) return 3;
  VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
  VkImageSubresource sr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0}; VkSubresourceLayout sl; vkGetImageSubresourceLayout(dev, img, &sr, &sl);
  int type = -1;
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    printf("memtype %u flags 0x%x heap %u%s\n", i, mp.memoryTypes[i].propertyFlags, mp.memoryTypes[i].heapIndex, (mr.memoryTypeBits >> i) & 1 ? " (allowed)" : "");
    if (type < 0 && ((mr.memoryTypeBits >> i) & 1) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) type = i;
  }
  if (want_type >= 0) type = want_type;
  VkExportMemoryAllocateInfo ex = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, NULL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
  VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &ex, mr.size, type};
  VkDeviceMemory mem; VkResult r = vkAllocateMemory(dev, &mai, NULL, &mem); printf("alloc type %d size %llu: %d rowPitch %llu\n", type, (unsigned long long)mr.size, r, (unsigned long long)sl.rowPitch); if (r) return 4;
  vkBindImageMemory(dev, img, mem, 0);
  uint8_t *p; vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&p);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) ((uint32_t *)(p + sl.offset + y * sl.rowPitch))[x] = 0xff00ff00; /* BGRA: green */
  vkUnmapMemory(dev, mem);
  PFN_vkGetMemoryFdKHR getfd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdKHR");
  VkMemoryGetFdInfoKHR gfi = {VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, NULL, mem, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
  int fd; if (getfd(dev, &gfi, &fd)) return 5;

  int dfd = open(node, O_RDWR); struct gbm_device *gbm = gbm_create_device(dfd);
  EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
  if (!eglInitialize(dpy, NULL, NULL)) return 6;
  printf("EGL vendor %s\n", eglQueryString(dpy, EGL_VENDOR));
  int core = getenv("CORE") != NULL;
  eglBindAPI(core ? EGL_OPENGL_API : EGL_OPENGL_ES_API);
  EGLint ca_es[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLint ca_core[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  EGLint *ca = core ? ca_core : ca_es;
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
  printf("GL renderer %s\n", glGetString(GL_RENDERER));
  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ = (void *)eglGetProcAddress("eglCreateImageKHR");
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
  PFNEGLQUERYDMABUFMODIFIERSEXTPROC qm = (void *)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
  { EGLuint64KHR mods[64]; EGLBoolean ext[64]; EGLint nm = 0;
    qm(dpy, DRM_FORMAT_XRGB8888, 64, mods, ext, &nm);
    for (int k = 0; k < nm; k++) printf("XRGB8888 modifier 0x%016llx external_only %d\n", (unsigned long long)mods[k], ext[k]); }
  for (int m = 0; m < 2; m++) {
    uint64_t mod = m ? DRM_FORMAT_MOD_LINEAR : DRM_FORMAT_MOD_INVALID;
    EGLint at[32]; int i = 0;
    at[i++] = EGL_WIDTH; at[i++] = W; at[i++] = EGL_HEIGHT; at[i++] = H;
    at[i++] = EGL_LINUX_DRM_FOURCC_EXT; at[i++] = DRM_FORMAT_XRGB8888;
    at[i++] = EGL_DMA_BUF_PLANE0_FD_EXT; at[i++] = fd;
    at[i++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT; at[i++] = sl.offset;
    at[i++] = EGL_DMA_BUF_PLANE0_PITCH_EXT; at[i++] = sl.rowPitch;
    if (mod != DRM_FORMAT_MOD_INVALID) { at[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT; at[i++] = mod & 0xffffffff; at[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT; at[i++] = mod >> 32; }
    at[i++] = EGL_NONE;
    EGLImageKHR im = eglCreateImageKHR_(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, at);
    if (im == EGL_NO_IMAGE_KHR) { printf("%s: eglCreateImage failed 0x%x\n", m ? "LINEAR" : "INVALID", eglGetError()); continue; }
    GLuint tex, fb; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glGetError(); glEGLImageTargetTexture2DOES_(GL_TEXTURE_2D, im);
    printf("%s: TEXTURE_2D target glerr 0x%x\n", m ? "LINEAR" : "INVALID", glGetError());
    GLuint tex2; glGenTextures(1, &tex2); glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex2);
    glEGLImageTargetTexture2DOES_(GL_TEXTURE_EXTERNAL_OES, im);
    printf("%s: TEXTURE_EXTERNAL_OES target glerr 0x%x\n", m ? "LINEAR" : "INVALID", glGetError());
    glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    uint8_t px[4] = {0}, px2[4] = {0};
    glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px2);
    printf("%s: fbstatus 0x%x glerr 0x%x pixel(10,10) RGBA %u,%u,%u,%u center %u,%u,%u,%u (want 0,255,0)\n", m ? "LINEAR" : "INVALID", glCheckFramebufferStatus(GL_FRAMEBUFFER), glGetError(), px[0], px[1], px[2], px[3], px2[0], px2[1], px2[2], px2[3]);
  }
  return 0;
}
