// Native-Vulkan mesh-shader probe: mesh stage (fullscreen triangle) +
// fragment, vkCmdDrawMeshTasksEXT, render-target readback.
// Usage: vk-mesh-probe <libMoltenVK.dylib>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static const int W = 64, H = 64;

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc < 2) { printf("usage: %s <libMoltenVK.dylib>\n", argv[0]); return 1; }

    void* h = dlopen(argv[1], RTLD_NOW|RTLD_LOCAL);
    if (!h) { printf("dlopen fail\n"); return 1; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(h,"vkGetInstanceProcAddr");
    PFN_vkCreateInstance vkci = (PFN_vkCreateInstance)gipa(NULL,"vkCreateInstance");
    VkInstance inst; VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char* instExts[] = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
    ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = instExts;
    if (vkci(&ici,NULL,&inst)!=VK_SUCCESS){printf("inst fail\n");return 1;}

    PFN_vkEnumeratePhysicalDevices epd=(PFN_vkEnumeratePhysicalDevices)gipa(inst,"vkEnumeratePhysicalDevices");
    uint32_t n=0; epd(inst,&n,NULL); VkPhysicalDevice p; epd(inst,&n,&p);

    // Check the mesh extension is advertised
    PFN_vkEnumerateDeviceExtensionProperties edep=(PFN_vkEnumerateDeviceExtensionProperties)gipa(inst,"vkEnumerateDeviceExtensionProperties");
    uint32_t extN = 0; edep(p,NULL,&extN,NULL);
    VkExtensionProperties* exts = malloc(extN*sizeof(VkExtensionProperties));
    edep(p,NULL,&extN,exts);
    int hasMesh = 0;
    for (uint32_t i = 0; i < extN; i++) if (!strcmp(exts[i].extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME)) hasMesh = 1;
    printf("VK_EXT_mesh_shader advertised: %s\n", hasMesh ? "YES" : "NO");
    free(exts);
    if (!hasMesh) { printf("RESULT: EXTENSION NOT ADVERTISED\n"); return 0; }

    // Features
    PFN_vkGetPhysicalDeviceFeatures2 gf2=(PFN_vkGetPhysicalDeviceFeatures2)gipa(inst,"vkGetPhysicalDeviceFeatures2");
    if (!gf2) gf2=(PFN_vkGetPhysicalDeviceFeatures2)gipa(inst,"vkGetPhysicalDeviceFeatures2KHR");
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeat={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext=&meshFeat;
    gf2(p,&f2);
    printf("meshShader=%d taskShader=%d\n", meshFeat.meshShader, meshFeat.taskShader);

    // Device with the mesh extension + features
    const char* devExts[] = { VK_EXT_MESH_SHADER_EXTENSION_NAME };
    float qprio = 1.0f;
    VkDeviceQueueCreateInfo dqc={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; dqc.queueFamilyIndex=0; dqc.queueCount=1; dqc.pQueuePriorities=&qprio;
    VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&dqc;
    dci.enabledExtensionCount=1; dci.ppEnabledExtensionNames=devExts;
    VkPhysicalDeviceMeshShaderFeaturesEXT mf={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    mf.meshShader = VK_TRUE; mf.taskShader = VK_TRUE;
    dci.pNext = &mf;
    PFN_vkCreateDevice vkcdev=(PFN_vkCreateDevice)gipa(inst,"vkCreateDevice");
    VkDevice dev;
    VkResult vr = vkcdev(p,&dci,NULL,&dev);
    printf("device create: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    if (vr != VK_SUCCESS) { printf("RESULT: DEVICE CREATE FAILED\n"); return 0; }
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst,"vkGetDeviceProcAddr");
    VkQueue q; ((PFN_vkGetDeviceQueue)gdpa(dev,"vkGetDeviceQueue"))(dev,0,0,&q);

    // Shaders (compiled by glslangValidator beforehand)
    FILE* mf_ = fopen("/tmp/vk-mesh-tri.spv","rb"); if (!mf_) { printf("MISSING /tmp/vk-mesh-tri.spv\n"); return 1; }
    fseek(mf_,0,SEEK_END); long msz=ftell(mf_); fseek(mf_,0,SEEK_SET);
    unsigned char* mblob = malloc(msz); fread(mblob,1,msz,mf_); fclose(mf_);
    FILE* ff_ = fopen("/tmp/vk-mesh-frag.spv","rb"); if (!ff_) { printf("MISSING /tmp/vk-mesh-frag.spv\n"); return 1; }
    fseek(ff_,0,SEEK_END); long fsz=ftell(ff_); fseek(ff_,0,SEEK_SET);
    unsigned char* fblob = malloc(fsz); fread(fblob,1,fsz,ff_); fclose(ff_);

    VkShaderModuleCreateInfo mci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mci.codeSize=msz; mci.pCode=(uint32_t*)mblob;
    VkShaderModule mmod; ((PFN_vkCreateShaderModule)gdpa(dev,"vkCreateShaderModule"))(dev,&mci,NULL,&mmod);
    mci.codeSize=fsz; mci.pCode=(uint32_t*)fblob;
    VkShaderModule fmod; ((PFN_vkCreateShaderModule)gdpa(dev,"vkCreateShaderModule"))(dev,&mci,NULL,&fmod);

    // Pipeline: mesh + fragment, no vertex input
    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_MESH_BIT_EXT; stages[0].module=mmod; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fmod; stages[1].pName="main";
    /* vkd3d-style layout: a descriptor set layout + push constants with the mesh stage */
    VkDescriptorSetLayoutBinding dslb = {0};
    dslb.binding=0; dslb.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dslb.descriptorCount=1;
    dslb.stageFlags=VK_SHADER_STAGE_MESH_BIT_EXT|VK_SHADER_STAGE_TASK_BIT_EXT|VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount=1; dslci.pBindings=&dslb;
    VkDescriptorSetLayout dsl; ((PFN_vkCreateDescriptorSetLayout)gdpa(dev,"vkCreateDescriptorSetLayout"))(dev,&dslci,NULL,&dsl);
    VkPushConstantRange pcr = {VK_SHADER_STAGE_MESH_BIT_EXT|VK_SHADER_STAGE_TASK_BIT_EXT, 0, 8};
    VkPipelineLayoutCreateInfo plci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount=1; plci.pSetLayouts=&dsl;
    plci.pushConstantRangeCount=1; plci.pPushConstantRanges=&pcr;
    VkPipelineLayout pl; ((PFN_vkCreatePipelineLayout)gdpa(dev,"vkCreatePipelineLayout"))(dev,&plci,NULL,&pl);
    printf("pipeline layout (mesh dsl): created\n");

    VkAttachmentDescription ad={0};
    ad.format=VK_FORMAT_R8G8B8A8_UNORM; ad.samples=VK_SAMPLE_COUNT_1_BIT;
    ad.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; ad.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    ad.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; ad.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkAttachmentReference ar={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp={0}; sp.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount=1; sp.pColorAttachments=&ar;
    VkRenderPassCreateInfo rpci={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rpci.attachmentCount=1; rpci.pAttachments=&ad; rpci.subpassCount=1; rpci.pSubpasses=&sp;
    VkRenderPass rp; ((PFN_vkCreateRenderPass)gdpa(dev,"vkCreateRenderPass"))(dev,&rpci,NULL,&rp);

    VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkViewport vport={0,0,(float)W,(float)H,0,1}; VkRect2D scissor={{0,0},{W,H}};
    vp.viewportCount=1; vp.pViewports=&vport; vp.scissorCount=1; vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.lineWidth=1.0f;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba={0}; cba.colorWriteMask=0xF;
    VkPipelineColorBlendStateCreateInfo cb={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount=1; cb.pAttachments=&cba;
    VkGraphicsPipelineCreateInfo gpi={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpi.stageCount=2; gpi.pStages=stages; gpi.pVertexInputState=&vi; gpi.pInputAssemblyState=&ia;
    gpi.pViewportState=&vp; gpi.pRasterizationState=&rs; gpi.pMultisampleState=&ms; gpi.pColorBlendState=&cb;
    gpi.layout=pl; gpi.renderPass=rp;
    /* dynamic-rendering variant: the vkd3d passes VkPipelineRenderingCreateInfo */
    VkPipelineRenderingCreateInfo rinfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rinfo.colorAttachmentCount=1; rinfo.pColorAttachmentFormats=(VkFormat[]){VK_FORMAT_R8G8B8A8_UNORM};
    VkGraphicsPipelineCreateInfo gpi2 = gpi;
    gpi2.renderPass = VK_NULL_HANDLE;
    gpi2.pNext = &rinfo;
    VkPipeline pipe, pipe2, pipe3, pipe4;
    vr = ((PFN_vkCreateGraphicsPipelines)gdpa(dev,"vkCreateGraphicsPipelines"))(dev,VK_NULL_HANDLE,1,&gpi,NULL,&pipe);
    printf("mesh pipeline create (renderpass): %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    vr = ((PFN_vkCreateGraphicsPipelines)gdpa(dev,"vkCreateGraphicsPipelines"))(dev,VK_NULL_HANDLE,1,&gpi2,NULL,&pipe2);
    printf("mesh pipeline create (dynamic rendering): %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    /* bisect the vkd3d-style fields one at a time */
    VkPipelineCreateFlags2CreateInfo flags2 = {VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
    VkPipelineDynamicStateCreateInfo dyn = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    VkDynamicState dyns[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dyn.dynamicStateCount=2; dyn.pDynamicStates=dyns;
    VkGraphicsPipelineCreateInfo gpi3;
    gpi3 = gpi2; gpi3.pNext = &flags2; flags2.pNext = &rinfo;
    vr = ((PFN_vkCreateGraphicsPipelines)gdpa(dev,"vkCreateGraphicsPipelines"))(dev,VK_NULL_HANDLE,1,&gpi3,NULL,&pipe3);
    printf("A flags2 pNext: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    gpi3 = gpi2; gpi3.pNext = &rinfo; gpi3.pDynamicState = &dyn;
    vr = ((PFN_vkCreateGraphicsPipelines)gdpa(dev,"vkCreateGraphicsPipelines"))(dev,VK_NULL_HANDLE,1,&gpi3,NULL,&pipe3);
    printf("B dynamic states: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    gpi3 = gpi2; gpi3.pNext = &rinfo; gpi3.pViewportState = NULL;
    vr = ((PFN_vkCreateGraphicsPipelines)gdpa(dev,"vkCreateGraphicsPipelines"))(dev,VK_NULL_HANDLE,1,&gpi3,NULL,&pipe3);
    printf("C NULL viewport: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    /* full vkd3d-style combo: flags2+rendering pNext, dynamic states, real blend, ds, dyn viewport */
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendStateCreateInfo cb2 = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState cba2 = {0}; cba2.colorWriteMask=0xF;
    cb2.attachmentCount=1; cb2.pAttachments=&cba2;
    VkPipelineViewportStateCreateInfo vp2 = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    gpi3 = gpi2;
    gpi3.pNext = &flags2; flags2.pNext = &rinfo;
    gpi3.pDynamicState = &dyn;
    gpi3.pViewportState = &vp2; /* dynamic viewport: count 0 */
    gpi3.pColorBlendState = &cb2;
    gpi3.pDepthStencilState = &ds;
    vr = ((PFN_vkCreateGraphicsPipelines)gdpa(dev,"vkCreateGraphicsPipelines"))(dev,VK_NULL_HANDLE,1,&gpi3,NULL,&pipe3);
    printf("E full vkd3d-style: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    if (vr != VK_SUCCESS) { printf("RESULT: MESH PIPELINE FAILED\n"); return 0; }

    // Image + framebuffer
    PFN_vkGetPhysicalDeviceMemoryProperties gmp=(PFN_vkGetPhysicalDeviceMemoryProperties)gipa(inst,"vkGetPhysicalDeviceMemoryProperties");
    VkPhysicalDeviceMemoryProperties mp; gmp(p,&mp);
    VkImageCreateInfo ici2={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici2.imageType=VK_IMAGE_TYPE_2D; ici2.format=VK_FORMAT_R8G8B8A8_UNORM;
    ici2.extent=(VkExtent3D){W,H,1}; ici2.mipLevels=1; ici2.arrayLayers=1; ici2.samples=VK_SAMPLE_COUNT_1_BIT;
    ici2.tiling=VK_IMAGE_TILING_OPTIMAL; ici2.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici2.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img; ((PFN_vkCreateImage)gdpa(dev,"vkCreateImage"))(dev,&ici2,NULL,&img);
    VkMemoryRequirements mr; ((PFN_vkGetImageMemoryRequirements)gdpa(dev,"vkGetImageMemoryRequirements"))(dev,img,&mr);
    uint32_t mi=0; for (uint32_t i=0;i<mp.memoryTypeCount;i++) if (mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT){mi=i;break;}
    VkMemoryAllocateInfo mai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize=mr.size; mai.memoryTypeIndex=mi;
    VkDeviceMemory imem; ((PFN_vkAllocateMemory)gdpa(dev,"vkAllocateMemory"))(dev,&mai,NULL,&imem);
    ((PFN_vkBindImageMemory)gdpa(dev,"vkBindImageMemory"))(dev,img,imem,0);
    VkImageViewCreateInfo ivi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivi.image=img; ivi.viewType=VK_IMAGE_VIEW_TYPE_2D; ivi.format=VK_FORMAT_R8G8B8A8_UNORM;
    ivi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageView iv; ((PFN_vkCreateImageView)gdpa(dev,"vkCreateImageView"))(dev,&ivi,NULL,&iv);
    VkFramebufferCreateInfo fbci={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass=rp; fbci.attachmentCount=1; fbci.pAttachments=&iv; fbci.width=W; fbci.height=H; fbci.layers=1;
    VkFramebuffer fb; ((PFN_vkCreateFramebuffer)gdpa(dev,"vkCreateFramebuffer"))(dev,&fbci,NULL,&fb);

    // Readback buffer
    VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size=W*H*4; bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer rb; ((PFN_vkCreateBuffer)gdpa(dev,"vkCreateBuffer"))(dev,&bci,NULL,&rb);
    VkMemoryRequirements rbm; ((PFN_vkGetBufferMemoryRequirements)gdpa(dev,"vkGetBufferMemoryRequirements"))(dev,rb,&rbm);
    uint32_t rmi=0; for (uint32_t i=0;i<mp.memoryTypeCount;i++) if (mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT){rmi=i;break;}
    VkMemoryAllocateInfo rmai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; rmai.allocationSize=rbm.size; rmai.memoryTypeIndex=rmi;
    VkDeviceMemory rmem; ((PFN_vkAllocateMemory)gdpa(dev,"vkAllocateMemory"))(dev,&rmai,NULL,&rmem);
    ((PFN_vkBindBufferMemory)gdpa(dev,"vkBindBufferMemory"))(dev,rb,rmem,0);

    // Command buffer
    VkCommandPoolCreateInfo cpci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpci.queueFamilyIndex=0;
    VkCommandPool cpool; ((PFN_vkCreateCommandPool)gdpa(dev,"vkCreateCommandPool"))(dev,&cpci,NULL,&cpool);
    VkCommandBufferAllocateInfo cbai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cbai.commandPool=cpool; cbai.commandBufferCount=1;
    VkCommandBuffer cmdbuf; ((PFN_vkAllocateCommandBuffers)gdpa(dev,"vkAllocateCommandBuffers"))(dev,&cbai,&cmdbuf);
    VkCommandBufferBeginInfo cbbi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ((PFN_vkBeginCommandBuffer)gdpa(dev,"vkBeginCommandBuffer"))(cmdbuf,&cbbi);
    VkRenderPassBeginInfo rpbi={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass=rp; rpbi.framebuffer=fb; rpbi.renderArea=(VkRect2D){{0,0},{W,H}};
    VkClearValue cv; cv.color=(VkClearColorValue){{0,0,0,0}}; rpbi.clearValueCount=1; rpbi.pClearValues=&cv;
    ((PFN_vkCmdBeginRenderPass)gdpa(dev,"vkCmdBeginRenderPass"))(cmdbuf,&rpbi,VK_SUBPASS_CONTENTS_INLINE);
    ((PFN_vkCmdBindPipeline)gdpa(dev,"vkCmdBindPipeline"))(cmdbuf,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
    ((PFN_vkCmdSetViewport)gdpa(dev,"vkCmdSetViewport"))(cmdbuf,0,1,&vport);
    ((PFN_vkCmdSetScissor)gdpa(dev,"vkCmdSetScissor"))(cmdbuf,0,1,&scissor);
    PFN_vkCmdDrawMeshTasksEXT drawMesh = (PFN_vkCmdDrawMeshTasksEXT)gdpa(dev,"vkCmdDrawMeshTasksEXT");
    printf("vkCmdDrawMeshTasksEXT resolved: %s\n", drawMesh ? "YES" : "NO");
    if (drawMesh) drawMesh(cmdbuf, 1, 1, 1);
    ((PFN_vkCmdEndRenderPass)gdpa(dev,"vkCmdEndRenderPass"))(cmdbuf);
    VkImageMemoryBarrier imb={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imb.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; imb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    imb.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; imb.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imb.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; imb.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    imb.image=img; imb.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    ((PFN_vkCmdPipelineBarrier)gdpa(dev,"vkCmdPipelineBarrier"))(cmdbuf,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&imb);
    VkBufferImageCopy bic={0};
    bic.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    bic.imageExtent=(VkExtent3D){W,H,1};
    ((PFN_vkCmdCopyImageToBuffer)gdpa(dev,"vkCmdCopyImageToBuffer"))(cmdbuf,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rb,1,&bic);
    ((PFN_vkEndCommandBuffer)gdpa(dev,"vkEndCommandBuffer"))(cmdbuf);
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&cmdbuf;
    ((PFN_vkQueueSubmit)gdpa(dev,"vkQueueSubmit"))(q,1,&si,VK_NULL_HANDLE);
    ((PFN_vkQueueWaitIdle)gdpa(dev,"vkQueueWaitIdle"))(q);

    void* map=NULL; ((PFN_vkMapMemory)gdpa(dev,"vkMapMemory"))(dev,rmem,0,VK_WHOLE_SIZE,0,&map);
    unsigned char* px=(unsigned char*)map;
    int c=0; for (int i=0;i<W*H*4;i+=4) if (px[i]||px[i+1]||px[i+2]||px[i+3]) c++;
    printf("fragments=%d / %d\n", c, W*H);
    printf("RESULT: %s\n", c > 0 ? "MESH DRAW PRODUCES FRAGMENTS" : "MESH DRAW: NO FRAGMENTS");
    return c > 0 ? 0 : 2;
}
