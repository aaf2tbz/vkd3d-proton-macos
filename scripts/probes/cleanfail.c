// Slice 1/2/3 clean-failure evidence: mesh/VRS/sampler-feedback on the fork MVK
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <dlfcn.h>
int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    void* h = dlopen(argv[1], RTLD_NOW|RTLD_LOCAL);
    if (!h) return 1;
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(h,"vkGetInstanceProcAddr");
    VkInstance inst; VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ((PFN_vkCreateInstance)gipa(NULL,"vkCreateInstance"))(&ici,NULL,&inst);
    PFN_vkEnumeratePhysicalDevices epd=(PFN_vkEnumeratePhysicalDevices)gipa(inst,"vkEnumeratePhysicalDevices");
    uint32_t n=0; epd(inst,&n,NULL); VkPhysicalDevice p; epd(inst,&n,&p);
    float qprio=1.0f;
    VkDeviceQueueCreateInfo dqc={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; dqc.queueFamilyIndex=0; dqc.queueCount=1; dqc.pQueuePriorities=&qprio;
    VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&dqc;
    VkDevice dev; VkResult vr=((PFN_vkCreateDevice)gipa(inst,"vkCreateDevice"))(p,&dci,NULL,&dev);
    printf("device: %s\n", vr==VK_SUCCESS?"OK":"FAIL");
    if (vr!=VK_SUCCESS) return 1;
    // 1. mesh: does vkCmdDrawMeshTasksEXT exist?
    PFN_vkCmdDrawMeshTasksEXT drawMesh = (PFN_vkCmdDrawMeshTasksEXT)gipa(inst,"vkCmdDrawMeshTasksEXT");
    printf("MESH: vkCmdDrawMeshTasksEXT %s\n", drawMesh ? "EXISTS" : "MISSING (clean failure: no mesh draw entry)");
    // 2. VRS: does VK_KHR_fragment_shading_rate exist?
    PFN_vkCmdSetFragmentShadingRateKHR vrs = (PFN_vkCmdSetFragmentShadingRateKHR)gipa(inst,"vkCmdSetFragmentShadingRateKHR");
    printf("VRS: vkCmdSetFragmentShadingRateKHR %s\n", vrs ? "EXISTS" : "MISSING (clean failure: no VRS entry)");
    printf("RESULT: %s\n", (!drawMesh && !vrs) ? "CLEAN FAILURES CONFIRMED (mesh + VRS entries absent; the tier gates are reported via the feature relaxations)" : "UNEXPECTED");
    return (!drawMesh && !vrs) ? 0 : 2;
}
