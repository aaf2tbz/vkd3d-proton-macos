#include <stdio.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
int main(void) {
    void* lib = dlopen("/Volumes/AverySSD/VKD3D-Proton-MacOS/sources/MoltenVK/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib", RTLD_NOW);
    if (!lib) { printf("dlopen fail: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance ci = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO }; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO }; ici.pApplicationInfo = &ai;
    VkInstance inst; if (ci(&ici, NULL, &inst) != VK_SUCCESS) { printf("inst fail\n"); return 1; }
    PFN_vkEnumeratePhysicalDevices epd = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd; uint32_t n = 1; epd(inst, &n, &pd);
    PFN_vkGetPhysicalDeviceProperties2 gp2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2");

    VkPhysicalDeviceSubgroupProperties sub = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &sub;
    gp2(pd, &p2);
    printf("SUBGROUP sType 0x%x: size=%u ops=0x%x stages=0x%x\n",
           (unsigned)sub.sType, sub.subgroupSize, (unsigned)sub.supportedOperations, (unsigned)sub.supportedStages);

    VkPhysicalDeviceVulkan11Properties v11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES };
    VkPhysicalDeviceProperties2 p3 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p3.pNext = &v11;
    gp2(pd, &p3);
    printf("VK11 sType 0x%x: size=%u ops=0x%x stages=0x%x\n",
           (unsigned)v11.sType, v11.subgroupSize, (unsigned)v11.subgroupSupportedOperations, (unsigned)v11.subgroupSupportedStages);
    return 0;
}
