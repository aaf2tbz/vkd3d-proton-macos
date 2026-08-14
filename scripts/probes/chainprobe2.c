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
    VkInstance inst; if (ci(&ici, NULL, &inst) != VK_SUCCESS) return 1;
    PFN_vkEnumeratePhysicalDevices epd = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd; uint32_t n = 1; epd(inst, &n, &pd);
    PFN_vkGetPhysicalDeviceFeatures2 gf2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(inst, "vkGetPhysicalDeviceFeatures2");
    VkPhysicalDeviceRobustness2FeaturesEXT fRb = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.pNext = &fRb;
    gf2(pd, &f2);
    printf("first-position: robustBufferAccess2=%u robustImageAccess2=%u nullDescriptor=%u\n",
           fRb.robustBufferAccess2, fRb.robustImageAccess2, fRb.nullDescriptor);
    return 0;
}
