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
    VkPhysicalDeviceFloatControlsProperties fc = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &fc;
    gp2(pd, &p2);
    printf("floatControls: independence=%d ftz32=%d preserve32=%d\n",
           (int)fc.denormBehaviorIndependence, (int)fc.shaderDenormFlushToZeroFloat32, (int)fc.shaderDenormPreserveFloat32);
    return 0;
}
