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
    printf("build: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    if (vr != VK_SUCCESS) { printf("RESULT: BUILD FAILED\n"); return 0; }

    // ===== TLAS: instance buffer (VkAccelerationStructureInstanceKHR) =====
    VkDeviceAddress blasAddr;
    {
        PFN_vkGetAccelerationStructureDeviceAddressKHR vkgasda=(PFN_vkGetAccelerationStructureDeviceAddressKHR)gipa(inst,"vkGetAccelerationStructureDeviceAddressKHR");
        VkAccelerationStructureDeviceAddressInfoKHR dai={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        dai.accelerationStructure = as;
        blasAddr = vkgasda(dev,&dai);
        printf("BLAS device address: 0x%llx\n", (unsigned long long)blasAddr);
    }
    VkAccelerationStructureInstanceKHR vkInst[1];
    memset(&vkInst[0], 0, sizeof(vkInst[0]));
    vkInst[0].transform.matrix[0][0] = 1.0f; vkInst[0].transform.matrix[1][1] = 1.0f; vkInst[0].transform.matrix[2][2] = 1.0f;
    vkInst[0].instanceCustomIndex = 7;          // userID 7
    vkInst[0].mask = 0xFF;
    vkInst[0].instanceShaderBindingTableRecordOffset = 0;
    vkInst[0].flags = 0;                        // no cull disable
    vkInst[0].accelerationStructureReference = blasAddr;
    VkBufferCreateInfo ibc={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ibc.size=4096; ibc.usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBuffer instBuf; vkcb(dev,&ibc,NULL,&instBuf);
    ((PFN_vkGetBufferMemoryRequirements)gipa(inst,"vkGetBufferMemoryRequirements"))(dev,instBuf,&mr);
    mai=(VkMemoryAllocateInfo){VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,NULL,mr.size,1};
    VkDeviceMemory instMem; vkam(dev,&mai,NULL,&instMem);
    ((PFN_vkBindBufferMemory)gipa(inst,"vkBindBufferMemory"))(dev,instBuf,instMem,0);
    void* idata; ((PFN_vkMapMemory)gipa(inst,"vkMapMemory"))(dev,instMem,0,VK_WHOLE_SIZE,0,&idata);
    memcpy(idata, vkInst, sizeof(vkInst));
    ((PFN_vkUnmapMemory)gipa(inst,"vkUnmapMemory"))(dev,instMem);

    // the TLAS
    VkAccelerationStructureCreateInfoKHR tasci={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    tasci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tasci.size = 4096;
    VkAccelerationStructureKHR tlas;
    vr = vkcas(dev,&tasci,NULL,&tlas);
    printf("TLAS create: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    VkAccelerationStructureGeometryInstancesDataKHR instData={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    VkDeviceAddress instAddr;
    { VkBufferDeviceAddressInfo bdai2={VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; bdai2.buffer=instBuf;
      instAddr = ((PFN_vkGetBufferDeviceAddress)gipa(inst,"vkGetBufferDeviceAddress"))(dev,&bdai2); }
    instData.data.deviceAddress = instAddr;
    VkAccelerationStructureGeometryKHR tgeom={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tgeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tgeom.geometry.instances = instData;
    tgeom.flags = 0;
    VkAccelerationStructureBuildGeometryInfoKHR tbg={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tbg.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tbg.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tbg.geometryCount = 1;
    tbg.pGeometries = &tgeom;
    tbg.dstAccelerationStructure = tlas;
    tbg.scratchData.deviceAddress = scrAddr;
    VkAccelerationStructureBuildRangeInfoKHR tbr = {1, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* tbrp = &tbr;
    VkAccelerationStructureBuildGeometryInfoKHR tbg2 = tbg;
    PFN_vkCmdBuildAccelerationStructuresKHR vkcabs2=(PFN_vkCmdBuildAccelerationStructuresKHR)gipa(inst,"vkCmdBuildAccelerationStructuresKHR");
    VkCommandBufferBeginInfo cbbiT={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkbcb(cb,&cbbiT);
    vkcabs2(cb, 1, &tbg2, &tbrp);
    vkecb(cb);
    VkSubmitInfo si1b={VK_STRUCTURE_TYPE_SUBMIT_INFO}; si1b.commandBufferCount=1; si1b.pCommandBuffers=&cb;
    vr = vkqs(q,1,&si1b,VK_NULL_HANDLE);
    printf("TLAS build submit: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    vkdwi(dev);
    if (vr != VK_SUCCESS) { printf("RESULT: TLAS BUILD FAILED\n"); return 0; }
    as = tlas;  // ray query against the TLAS now

    // ===== inline ray query: descriptor set + compute pipeline + dispatch =====
    // the hits buffer (SSBO, binding 1)
    bc.size = 64*64*4; bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer hitBuf; vkcb(dev,&bc,NULL,&hitBuf);
    ((PFN_vkGetBufferMemoryRequirements)gipa(inst,"vkGetBufferMemoryRequirements"))(dev,hitBuf,&mr);
    mai=(VkMemoryAllocateInfo){VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,NULL,mr.size,1};
    VkDeviceMemory hitMem; vkam(dev,&mai,NULL,&hitMem);
    ((PFN_vkBindBufferMemory)gipa(inst,"vkBindBufferMemory"))(dev,hitBuf,hitMem,0);

    // the ray query shader (hand-assembled SPIR-V)
    const char* shaderPath = getenv("RQ_SHADER") ? getenv("RQ_SHADER") : "/tmp/rq-compute.spv";
    FILE* rf = fopen(shaderPath,"rb");
    fseek(rf,0,SEEK_END); long rsz=ftell(rf); fseek(rf,0,SEEK_SET);
    uint32_t* rcode = malloc(rsz);
    fread(rcode,1,rsz,rf); fclose(rf);
    VkShaderModuleCreateInfo rsmc={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    rsmc.codeSize=rsz; rsmc.pCode=rcode;
    VkShaderModule rsm; PFN_vkCreateShaderModule vkcsm=(PFN_vkCreateShaderModule)gipa(inst,"vkCreateShaderModule");
    vr = vkcsm(dev,&rsmc,NULL,&rsm);
    printf("shader [%s] module: %s (%d)\n", getenv("RQ_SHADER")?getenv("RQ_SHADER"):"rq", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);

    // the descriptor set layout: binding 0 = AS, binding 1 = SSBO
    VkDescriptorSetLayoutBinding dslb[2] = {
        {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dsli={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,NULL,0,2,dslb};
    PFN_vkCreateDescriptorSetLayout vkcdsl=(PFN_vkCreateDescriptorSetLayout)gipa(inst,"vkCreateDescriptorSetLayout");
    VkDescriptorSetLayout dsl; vr = vkcdsl(dev,&dsli,NULL,&dsl);
    printf("dsl: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    VkPipelineLayoutCreateInfo plc={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plc.setLayoutCount=1; plc.pSetLayouts=&dsl;
    PFN_vkCreatePipelineLayout vkcpl=(PFN_vkCreatePipelineLayout)gipa(inst,"vkCreatePipelineLayout");
    VkPipelineLayout pl; vkcpl(dev,&plc,NULL,&pl);
    VkPipelineShaderStageCreateInfo st={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    st.stage=VK_SHADER_STAGE_COMPUTE_BIT; st.module=rsm; st.pName="main";
    VkComputePipelineCreateInfo cpc={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpc.stage=st; cpc.layout=pl;
    PFN_vkCreateComputePipelines vkccp2=(PFN_vkCreateComputePipelines)gipa(inst,"vkCreateComputePipelines");
    VkPipeline rqpipe;
    vr = vkccp2(dev,VK_NULL_HANDLE,1,&cpc,NULL,&rqpipe);
    printf("ray query pipeline: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    if (vr != VK_SUCCESS) { printf("RESULT: RAY QUERY PIPELINE FAILED\n"); return 0; }

    // the descriptor set + the write (AS via the pNext)
    VkDescriptorPoolSize dps[2] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpci={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,NULL,0,1,2,dps};
    PFN_vkCreateDescriptorPool vkcdp=(PFN_vkCreateDescriptorPool)gipa(inst,"vkCreateDescriptorPool");
    VkDescriptorPool dp; vkcdp(dev,&dpci,NULL,&dp);
    VkDescriptorSetAllocateInfo dsai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool=dp; dsai.descriptorSetCount=1; dsai.pSetLayouts=&dsl;
    VkDescriptorSet ds; PFN_vkAllocateDescriptorSets vkads=(PFN_vkAllocateDescriptorSets)gipa(inst,"vkAllocateDescriptorSets"); vkads(dev,&dsai,&ds);
    VkWriteDescriptorSetAccelerationStructureKHR wdsAS={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    wdsAS.accelerationStructureCount=1; wdsAS.pAccelerationStructures=&as;
    VkWriteDescriptorSet wds[2] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,ds,0,0,1,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,NULL,NULL,NULL},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,ds,1,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,NULL,&(VkDescriptorBufferInfo){hitBuf,0,VK_WHOLE_SIZE},NULL}};
    wds[0].pNext = &wdsAS;
    PFN_vkUpdateDescriptorSets vkuds=(PFN_vkUpdateDescriptorSets)gipa(inst,"vkUpdateDescriptorSets");
    vkuds(dev,2,wds,0,NULL);
    printf("descriptor write: done\n");
    // dump the descriptor set's GPU buffer (the MVK argument-table data)
    { PFN_vkGetDescriptorSetLayoutSupport gdsls=(PFN_vkGetDescriptorSetLayoutSupport)gipa(inst,"vkGetDescriptorSetLayoutSupport"); (void)gdsls;
      // read the set's internal buffer via the probe's own copy of the write data:
      // (the MVK's descriptor data is internal; verify via the shader readback instead) }
    // verify the instance data conversion: read the converted MTL descriptors from the MVK's temp buffer
    // (not directly reachable - verify via the BLAS/TLAS ids and the hit behavior)

    // dispatch
    VkCommandBuffer cb2; vkacb(dev,&cbai,&cb2);
    VkCommandBufferBeginInfo cbbi2={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkbcb(cb2,&cbbi2);
    PFN_vkCmdBindPipeline vkcbp2=(PFN_vkCmdBindPipeline)gipa(inst,"vkCmdBindPipeline");
    vkcbp2(cb2,VK_PIPELINE_BIND_POINT_COMPUTE,rqpipe);
    PFN_vkCmdBindDescriptorSets vkcbds=(PFN_vkCmdBindDescriptorSets)gipa(inst,"vkCmdBindDescriptorSets");
    vkcbds(cb2,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&ds,0,NULL);
    PFN_vkCmdDispatch vkcdsp=(PFN_vkCmdDispatch)gipa(inst,"vkCmdDispatch");
    vkcdsp(cb2,8,8,1);
    vkecb(cb2);
    VkSubmitInfo si2={VK_STRUCTURE_TYPE_SUBMIT_INFO}; si2.commandBufferCount=1; si2.pCommandBuffers=&cb2;
    vr = vkqs(q,1,&si2,VK_NULL_HANDLE);
    printf("dispatch submit: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    vkdwi(dev);

    // readback
    void* rdata; ((PFN_vkMapMemory)gipa(inst,"vkMapMemory"))(dev,hitMem,0,VK_WHOLE_SIZE,0,&rdata);
    float* hp = (float*)rdata;
    int hits = 0; float minD = 1e9;
    for (int i = 0; i < 64*64; i++) { if (hp[i] > 0) { hits++; if (hp[i] < minD) minD = hp[i]; } }
    printf("ray query hits: %d minD=%.3f\n", hits, minD);
    printf("RESULT: %s\n", hits > 0 ? "INLINE RAY QUERY (FULL VULKAN PATH) WORKS" : "RAY QUERY NO HITS");
    return 0;
}
