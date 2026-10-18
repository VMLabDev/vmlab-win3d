/* Print linearTilingFeatures (v1 and VkFormatProperties3) of a few colour
 * formats and whether COLOR_ATTACHMENT is among them, for device 0.
 * Build: cc -o formatfeatures formatfeatures.c -lvulkan */
#include <vulkan/vulkan.h>
#include <stdio.h>
int main(void){VkApplicationInfo a={VK_STRUCTURE_TYPE_APPLICATION_INFO,0,"x",0,0,0,VK_API_VERSION_1_3};VkInstanceCreateInfo i={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,0,0,&a};VkInstance in;vkCreateInstance(&i,0,&in);uint32_t n=4;VkPhysicalDevice p[4];vkEnumeratePhysicalDevices(in,&n,p);
VkFormat f[]={VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_R8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT};
VkPhysicalDeviceProperties pr;vkGetPhysicalDeviceProperties(p[0],&pr);printf("%s\n",pr.deviceName);
for(int k=0;k<5;k++){VkFormatProperties3 f3={VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};VkFormatProperties2 f2={VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,&f3};vkGetPhysicalDeviceFormatProperties2(p[0],f[k],&f2);
printf("fmt %d linear v1 0x%x v3 0x%llx color_attachment(linear) %s\n",f[k],f2.formatProperties.linearTilingFeatures,(unsigned long long)f3.linearTilingFeatures,(f3.linearTilingFeatures&0x80)?"yes":"no");}}
