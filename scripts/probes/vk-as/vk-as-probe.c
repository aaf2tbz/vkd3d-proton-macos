// Vulkan-level acceleration structure build probe: creates a BLAS through
// the MVK's VK_KHR_acceleration_structure path (sizes, create, build) and
// verifies the MTL4 build executes.
// Usage: vk-as-probe <libMoltenVK.dylib>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: %s <libMoltenVK.dylib>\n", argv[0]); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    void* h = dlopen(argv[1], RTLD_NOW|RTLD_LOCAL);
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(h,"vkGetInstanceProcAddr");
    VkInstance inst; VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    PFN_vkCreateInstance vkci = (PFN_vkCreateInstance)gipa(NULL,"vkCreateInstance");
    if (vkci(&ici,NULL,&inst)!=VK_SUCCESS){printf("inst fail\n");return 1;}
    PFN_vkEnumeratePhysicalDevices epd=(PFN_vkEnumeratePhysicalDevices)gipa(inst,"vkEnumeratePhysicalDevices");
    uint32_t n=0; epd(inst,&n,NULL); VkPhysicalDevice p; epd(inst,&n,&p);

    // device with the RT extensions + features
    const char* devExts[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME };
    float qprio = 1.0f;
    VkDeviceQueueCreateInfo dqc={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; dqc.queueFamilyIndex=0; dqc.queueCount=1; dqc.pQueuePriorities=&qprio;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeat={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    asFeat.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR rqFeat={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    asFeat.pNext = &rqFeat;
    rqFeat.rayQuery = VK_TRUE;
    VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&dqc;
    dci.enabledExtensionCount=2; dci.ppEnabledExtensionNames=devExts;
    dci.pNext = &asFeat;
    PFN_vkCreateDevice vkcdev=(PFN_vkCreateDevice)gipa(inst,"vkCreateDevice");
    VkDevice dev; VkResult vr = vkcdev(p,&dci,NULL,&dev);
    printf("device create (RT): %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    if (vr != VK_SUCCESS) { printf("RESULT: DEVICE CREATE FAILED\n"); return 0; }
    VkQueue q; ((PFN_vkGetDeviceQueue)gipa(inst,"vkGetDeviceQueue"))(dev,0,0,&q);

    // the geometry: one triangle (0,0,-5),(4,0,-5),(0,4,-5)
    float verts[9] = { 0,0,-5, 4,0,-5, 0,4,-5 };
    uint32_t indices[3] = { 0, 1, 2 };
    VkBufferCreateInfo bc={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size=4096; bc.usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bc.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    PFN_vkCreateBuffer vkcb=(PFN_vkCreateBuffer)gipa(inst,"vkCreateBuffer");
    VkBuffer vb; vkcb(dev,&bc,NULL,&vb);
    VkMemoryRequirements mr; ((PFN_vkGetBufferMemoryRequirements)gipa(inst,"vkGetBufferMemoryRequirements"))(dev,vb,&mr);
    VkMemoryAllocateInfo mai=(VkMemoryAllocateInfo){VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,NULL,mr.size,1};
    VkDeviceMemory vmem; PFN_vkAllocateMemory vkam=(PFN_vkAllocateMemory)gipa(inst,"vkAllocateMemory"); vkam(dev,&mai,NULL,&vmem);
    ((PFN_vkBindBufferMemory)gipa(inst,"vkBindBufferMemory"))(dev,vb,vmem,0);
    void* data; ((PFN_vkMapMemory)gipa(inst,"vkMapMemory"))(dev,vmem,0,VK_WHOLE_SIZE,0,&data);
    memcpy(data, verts, 36); memcpy((char*)data+512, indices, 12);
    ((PFN_vkUnmapMemory)gipa(inst,"vkUnmapMemory"))(dev,vmem);
    VkBufferDeviceAddressInfo bdai={VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    bdai.buffer=vb;
    PFN_vkGetBufferDeviceAddress vkgbda=(PFN_vkGetBufferDeviceAddress)gipa(inst,"vkGetBufferDeviceAddress");
    VkDeviceAddress vAddr = vkgbda(dev,&bdai);
    printf("vertex buffer address: 0x%llx\n", (unsigned long long)vAddr);

    // build sizes
    VkAccelerationStructureGeometryTrianglesDataKHR tri={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = vAddr;
    tri.vertexStride = 12;
    tri.maxVertex = 2;
    tri.indexType = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress = vAddr + 512;
    VkAccelerationStructureGeometryKHR geom={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.geometry.triangles = tri;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR bg={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bg.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bg.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bg.geometryCount = 1;
    bg.pGeometries = &geom;
    uint32_t maxPrim = 1;
    VkAccelerationStructureBuildSizesInfoKHR sizes={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    PFN_vkGetAccelerationStructureBuildSizesKHR vkgabs=(PFN_vkGetAccelerationStructureBuildSizesKHR)gipa(inst,"vkGetAccelerationStructureBuildSizesKHR");
    vkgabs(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bg, &maxPrim, &sizes);
    printf("AS size: %llu scratch: %llu\n", (unsigned long long)sizes.accelerationStructureSize, (unsigned long long)sizes.buildScratchSize);

    // the AS storage buffer + the scratch buffer
    bc.size = sizes.accelerationStructureSize + 4096;
    bc.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    VkBuffer asBuf; vkcb(dev,&bc,NULL,&asBuf);
    ((PFN_vkGetBufferMemoryRequirements)gipa(inst,"vkGetBufferMemoryRequirements"))(dev,asBuf,&mr);
    mai=(VkMemoryAllocateInfo){VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,NULL,mr.size,0};
    VkDeviceMemory asMem; vkam(dev,&mai,NULL,&asMem);
    ((PFN_vkBindBufferMemory)gipa(inst,"vkBindBufferMemory"))(dev,asBuf,asMem,0);
    bc.size = sizes.buildScratchSize + 4096;
    bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBuffer scrBuf; vkcb(dev,&bc,NULL,&scrBuf);
    ((PFN_vkGetBufferMemoryRequirements)gipa(inst,"vkGetBufferMemoryRequirements"))(dev,scrBuf,&mr);
    mai=(VkMemoryAllocateInfo){VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,NULL,mr.size,1};
    VkDeviceMemory scrMem; vkam(dev,&mai,NULL,&scrMem);
    ((PFN_vkBindBufferMemory)gipa(inst,"vkBindBufferMemory"))(dev,scrBuf,scrMem,0);
    bdai.buffer=scrBuf;
    VkDeviceAddress scrAddr = vkgbda(dev,&bdai);

    // create the AS
    VkAccelerationStructureCreateInfoKHR aci={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    aci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    aci.buffer = asBuf;
    aci.size = sizes.accelerationStructureSize;
    VkAccelerationStructureKHR as;
    PFN_vkCreateAccelerationStructureKHR vkcas=(PFN_vkCreateAccelerationStructureKHR)gipa(inst,"vkCreateAccelerationStructureKHR");
    vr = vkcas(dev,&aci,NULL,&as);
    printf("AS create: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    if (vr != VK_SUCCESS) { printf("RESULT: AS CREATE FAILED\n"); return 0; }

    // build
    bg.dstAccelerationStructure = as;
    bg.scratchData.deviceAddress = scrAddr;
    VkAccelerationStructureBuildRangeInfoKHR range={1,0,0,0};
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
    PFN_vkCmdBuildAccelerationStructuresKHR vkcbas=(PFN_vkCmdBuildAccelerationStructuresKHR)gipa(inst,"vkCmdBuildAccelerationStructuresKHR");
    PFN_vkCreateCommandPool vkccp=(PFN_vkCreateCommandPool)gipa(inst,"vkCreateCommandPool");
    VkCommandPool cp; VkCommandPoolCreateInfo cpci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpci.queueFamilyIndex=0;
    vkccp(dev,&cpci,NULL,&cp);
    PFN_vkAllocateCommandBuffers vkacb=(PFN_vkAllocateCommandBuffers)gipa(inst,"vkAllocateCommandBuffers");
    VkCommandBuffer cb; VkCommandBufferAllocateInfo cbai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cbai.commandPool=cp; cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount=1;
    vkacb(dev,&cbai,&cb);
    PFN_vkBeginCommandBuffer vkbcb=(PFN_vkBeginCommandBuffer)gipa(inst,"vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer vkecb=(PFN_vkEndCommandBuffer)gipa(inst,"vkEndCommandBuffer");
    VkCommandBufferBeginInfo cbbi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkbcb(cb,&cbbi);
    vkcbas(cb, 1, &bg, &pRange);
    vkecb(cb);
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&cb;
    PFN_vkQueueSubmit vkqs=(PFN_vkQueueSubmit)gipa(inst,"vkQueueSubmit");
    PFN_vkDeviceWaitIdle vkdwi=(PFN_vkDeviceWaitIdle)gipa(inst,"vkDeviceWaitIdle");
    vr = vkqs(q,1,&si,VK_NULL_HANDLE);
    printf("submit: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    vkdwi(dev);
    printf("RESULT: %s\n", vr==VK_SUCCESS ? "ACCELERATION STRUCTURE BUILD PATH WORKS" : "BUILD FAILED");
    return 0;
}
