#include <stdio.h>
#include <dlfcn.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
int main(int argc, char** argv) {
    void* lib = dlopen(argv[1], RTLD_NOW);
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance ci = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO }; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO }; ici.pApplicationInfo = &ai;
    VkInstance inst; ci(&ici, NULL, &inst);
    PFN_vkEnumeratePhysicalDevices epd = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd; uint32_t n = 1; epd(inst, &n, &pd);
    PFN_vkGetPhysicalDeviceProperties2 gp2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2");
    VkPhysicalDeviceVulkan12Properties p12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &p12;
    gp2(pd, &p2);
    printf("denormBehaviorIndependence=%d shaderDenormPreserveFloat32=%d shaderDenormFlushToZeroFloat32=%d driverID=%d\n",
           (int)p12.denormBehaviorIndependence, (int)p12.shaderDenormPreserveFloat32,
           (int)p12.shaderDenormFlushToZeroFloat32);
    return 0;
}
