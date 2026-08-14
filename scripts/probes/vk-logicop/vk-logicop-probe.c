/* Vulkan logic-op emulation probe (render-pass based, like the vkd3d/D3D12 route).
 * Seeded BGRA8 attachment (R=0xAA); fullscreen triangle writes src=(0.1,0.2,0.3,0.4)
 * through a pipeline with logicOpEnable=XOR; verifies src^dst per 8-bit channel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

static void *lib;
static PFN_vkGetInstanceProcAddr gipa;
static VkInstance inst;
static VkPhysicalDevice pd;
static VkDevice dev;

#define LOADDEV(T, name) T name = (T)gipa(inst, #name)
#define LOADDEV_REQ(T, name) \
    T name = (T)gipa(inst, #name); \
    if (!name) { fprintf(stderr, "missing %s\n", #name); return 1; }

static uint8_t *read_spv(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) { perror("fread"); exit(1); }
    fclose(f); *len = sz; return buf;
}

int main(int argc, char **argv) {
    const char *dylib = argc > 1 ? argv[1] : "libMoltenVK.dylib";
    const char *vspvPath = argc > 2 ? argv[2] : "vert.spv";
    const char *fspvPath = argc > 3 ? argv[3] : "frag.spv";
    lib = dlopen(dylib, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    if (!gipa) { fprintf(stderr, "no gipa\n"); return 1; }

    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &ai;
    LOADDEV_REQ(PFN_vkCreateInstance, vkCreateInstance);
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) { fprintf(stderr, "inst fail\n"); return 1; }
    LOADDEV_REQ(PFN_vkEnumeratePhysicalDevices, vkEnumeratePhysicalDevices);
    uint32_t n = 1;
    if (vkEnumeratePhysicalDevices(inst, &n, &pd) != VK_SUCCESS || n == 0) { fprintf(stderr, "no pd\n"); return 1; }

    LOADDEV_REQ(PFN_vkGetPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceQueueFamilyProperties);
    uint32_t qfc = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfc, NULL);
    VkQueueFamilyProperties *qfp = calloc(qfc, sizeof(*qfp));
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfc, qfp);
    uint32_t qf = 0;
    for (uint32_t i = 0; i < qfc; i++) if (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qf = i; break; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo dq = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    dq.queueFamilyIndex = qf; dq.queueCount = 1; dq.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &dq;
    LOADDEV_REQ(PFN_vkCreateDevice, vkCreateDevice);
    if (vkCreateDevice(pd, &dci, NULL, &dev) != VK_SUCCESS) { fprintf(stderr, "dev fail\n"); return 1; }
    LOADDEV(PFN_vkGetDeviceQueue, vkGetDeviceQueue);
    VkQueue queue; vkGetDeviceQueue(dev, qf, 0, &queue);

    LOADDEV_REQ(PFN_vkGetPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceMemoryProperties);
    VkPhysicalDeviceMemoryProperties mprops; vkGetPhysicalDeviceMemoryProperties(pd, &mprops);
    uint32_t devType = UINT32_MAX, hostType = UINT32_MAX;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++) {
        if (mprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) devType = i;
        if ((mprops.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) hostType = i;
    }

    LOADDEV_REQ(PFN_vkCreateShaderModule, vkCreateShaderModule);
    size_t vsz, fsz;
    uint8_t *vspv = read_spv(vspvPath, &vsz), *fspv = read_spv(fspvPath, &fsz);
    VkShaderModule vsm, fsm;
    VkShaderModuleCreateInfo smc = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smc.codeSize = vsz; smc.pCode = (uint32_t*)vspv; vkCreateShaderModule(dev, &smc, NULL, &vsm);
    smc.codeSize = fsz; smc.pCode = (uint32_t*)fspv; vkCreateShaderModule(dev, &smc, NULL, &fsm);

    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vsm, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fsm, "main", NULL },
    };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkViewport viewport = { 0, 0, 64, 64, 0, 1 };
    VkRect2D scissor = { {0,0}, {64,64} };
    vp.viewportCount = 1; vp.pViewports = &viewport;
    vp.scissorCount = 1; vp.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba = { 0 };
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ba.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    if (!getenv("LO_OFF")) {
        cb.logicOpEnable = VK_TRUE;
        cb.logicOp = VK_LOGIC_OP_XOR;
    }
    cb.attachmentCount = 1; cb.pAttachments = &ba;

    VkAttachmentDescription att = { 0 };
    att.format = VK_FORMAT_B8G8R8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { 0 };
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 1; rpci.pAttachments = &att;
    rpci.subpassCount = 1; rpci.pSubpasses = &sub;
    LOADDEV_REQ(PFN_vkCreateRenderPass, vkCreateRenderPass);
    VkRenderPass renderPass;
    if (vkCreateRenderPass(dev, &rpci, NULL, &renderPass) != VK_SUCCESS) { fprintf(stderr, "rp fail\n"); return 1; }

    VkPipelineLayoutCreateInfo plc = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    LOADDEV_REQ(PFN_vkCreatePipelineLayout, vkCreatePipelineLayout);
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(dev, &plc, NULL, &layout) != VK_SUCCESS) { fprintf(stderr, "layout fail\n"); return 1; }

    VkGraphicsPipelineCreateInfo gpc = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpc.stageCount = 2; gpc.pStages = stages;
    gpc.pVertexInputState = &vi; gpc.pInputAssemblyState = &ia;
    gpc.pViewportState = &vp; gpc.pRasterizationState = &rs;
    gpc.pMultisampleState = &ms; gpc.pColorBlendState = &cb;
    gpc.layout = layout; gpc.renderPass = renderPass;
    LOADDEV_REQ(PFN_vkCreateGraphicsPipelines, vkCreateGraphicsPipelines);
    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpc, NULL, &pipeline) != VK_SUCCESS) {
        fprintf(stderr, "pipeline fail\n"); return 1;
    }
    fprintf(stderr, "pipeline created\n");

    LOADDEV_REQ(PFN_vkCreateImage, vkCreateImage);
    LOADDEV_REQ(PFN_vkGetImageMemoryRequirements, vkGetImageMemoryRequirements);
    LOADDEV_REQ(PFN_vkAllocateMemory, vkAllocateMemory);
    LOADDEV_REQ(PFN_vkBindImageMemory, vkBindImageMemory);
    VkImageCreateInfo ic = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_B8G8R8A8_UNORM;
    ic.extent = (VkExtent3D){64, 64, 1}; ic.mipLevels = 1; ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img;
    if (vkCreateImage(dev, &ic, NULL, &img) != VK_SUCCESS) { fprintf(stderr, "image fail\n"); return 1; }
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size; mai.memoryTypeIndex = devType;
    VkDeviceMemory mem;
    if (vkAllocateMemory(dev, &mai, NULL, &mem) != VK_SUCCESS) { fprintf(stderr, "mem fail\n"); return 1; }
    vkBindImageMemory(dev, img, mem, 0);

    LOADDEV_REQ(PFN_vkCreateImageView, vkCreateImageView);
    VkImageViewCreateInfo ivc = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivc.image = img; ivc.viewType = VK_IMAGE_VIEW_TYPE_2D; ivc.format = VK_FORMAT_B8G8R8A8_UNORM;
    ivc.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageView view; vkCreateImageView(dev, &ivc, NULL, &view);
    LOADDEV_REQ(PFN_vkCreateFramebuffer, vkCreateFramebuffer);
    VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbci.renderPass = renderPass; fbci.attachmentCount = 1; fbci.pAttachments = &view;
    fbci.width = 64; fbci.height = 64; fbci.layers = 1;
    VkFramebuffer fb;
    if (vkCreateFramebuffer(dev, &fbci, NULL, &fb) != VK_SUCCESS) { fprintf(stderr, "fb fail\n"); return 1; }

    LOADDEV_REQ(PFN_vkCreateBuffer, vkCreateBuffer);
    LOADDEV_REQ(PFN_vkGetBufferMemoryRequirements, vkGetBufferMemoryRequirements);
    LOADDEV_REQ(PFN_vkBindBufferMemory, vkBindBufferMemory);
    LOADDEV_REQ(PFN_vkMapMemory, vkMapMemory);
    LOADDEV_REQ(PFN_vkUnmapMemory, vkUnmapMemory);
    VkBufferCreateInfo bc = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = 64 * 64 * 4; bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf; vkCreateBuffer(dev, &bc, NULL, &buf);
    VkMemoryRequirements bmr; vkGetBufferMemoryRequirements(dev, buf, &bmr);
    VkMemoryAllocateInfo bai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    bai.allocationSize = bmr.size; bai.memoryTypeIndex = hostType;
    VkDeviceMemory bmem; vkAllocateMemory(dev, &bai, NULL, &bmem);
    vkBindBufferMemory(dev, buf, bmem, 0);

    LOADDEV_REQ(PFN_vkCreateCommandPool, vkCreateCommandPool);
    LOADDEV_REQ(PFN_vkAllocateCommandBuffers, vkAllocateCommandBuffers);
    LOADDEV_REQ(PFN_vkBeginCommandBuffer, vkBeginCommandBuffer);
    LOADDEV_REQ(PFN_vkEndCommandBuffer, vkEndCommandBuffer);
    LOADDEV_REQ(PFN_vkCmdPipelineBarrier2, vkCmdPipelineBarrier2);
    LOADDEV_REQ(PFN_vkCmdCopyBufferToImage, vkCmdCopyBufferToImage);
    LOADDEV_REQ(PFN_vkCmdCopyImageToBuffer, vkCmdCopyImageToBuffer);
    LOADDEV_REQ(PFN_vkCmdBeginRenderPass, vkCmdBeginRenderPass);
    LOADDEV_REQ(PFN_vkCmdEndRenderPass, vkCmdEndRenderPass);
    LOADDEV_REQ(PFN_vkCmdBindPipeline, vkCmdBindPipeline);
    LOADDEV_REQ(PFN_vkCmdDraw, vkCmdDraw);
    LOADDEV_REQ(PFN_vkQueueSubmit, vkQueueSubmit);
    LOADDEV_REQ(PFN_vkDeviceWaitIdle, vkDeviceWaitIdle);

    VkCommandPoolCreateInfo cpc = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpc.queueFamilyIndex = qf;
    VkCommandPool pool; vkCreateCommandPool(dev, &cpc, NULL, &pool);
    VkCommandBufferAllocateInfo cba = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cba.commandPool = pool; cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cba.commandBufferCount = 1;
    VkCommandBuffer cmdbuf; vkAllocateCommandBuffers(dev, &cba, &cmdbuf);
    VkCommandBufferBeginInfo cbb = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    void *data; vkMapMemory(dev, bmem, 0, VK_WHOLE_SIZE, 0, &data);
    uint32_t seed = 0x11223344; // BGRA bytes: B=44 G=33 R=22 A=11
    for (int i = 0; i < 64*64; i++) ((uint32_t*)data)[i] = seed;
    vkUnmapMemory(dev, bmem);

    vkBeginCommandBuffer(cmdbuf, &cbb);
    VkBufferImageCopy bic = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, {0,0,0}, {64,64,1} };
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };

    VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpbi.renderPass = renderPass; rpbi.framebuffer = fb;
    rpbi.renderArea = (VkRect2D){ {0,0}, {64,64} };
    VkClearValue clear = { .color = { 1.0f, 0.0f, 0.0f, 1.0f } };
    rpbi.clearValueCount = 1; rpbi.pClearValues = &clear;
    if (getenv("NO_RP")) {
        // round-trip: buffer->image->buffer without a render pass
        VkImageMemoryBarrier2 ib0 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib0.srcStageMask = VK_PIPELINE_STAGE_2_NONE; ib0.srcAccessMask = 0;
        ib0.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; ib0.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        ib0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ib0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ib0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib0.image = img; ib0.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep0 = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep0.imageMemoryBarrierCount = 1; dep0.pImageMemoryBarriers = &ib0;
        vkCmdPipelineBarrier2(cmdbuf, &dep0);
        vkCmdCopyBufferToImage(cmdbuf, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
        VkImageMemoryBarrier2 ib1 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib1.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; ib1.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        ib1.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; ib1.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        ib1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ib1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib1.image = img; ib1.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep1 = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep1.imageMemoryBarrierCount = 1; dep1.pImageMemoryBarriers = &ib1;
        vkCmdPipelineBarrier2(cmdbuf, &dep1);
        goto readback;
    }
    // touch the image with a transfer write first (mirrors the NO_RP working path)
    {
        VkImageMemoryBarrier2 ibT = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ibT.srcStageMask = VK_PIPELINE_STAGE_2_NONE; ibT.srcAccessMask = 0;
        ibT.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; ibT.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        ibT.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ibT.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ibT.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ibT.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ibT.image = img; ibT.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo depT = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depT.imageMemoryBarrierCount = 1; depT.pImageMemoryBarriers = &ibT;
        vkCmdPipelineBarrier2(cmdbuf, &depT);
        vkCmdCopyBufferToImage(cmdbuf, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
    }
    vkCmdBeginRenderPass(cmdbuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    if (!getenv("NO_DRAW")) {
        vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(cmdbuf, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmdbuf);
    // Submit the render pass, then copy the image out in a second command buffer.
    vkEndCommandBuffer(cmdbuf);
    {
        VkSubmitInfo siA = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        siA.commandBufferCount = 1; siA.pCommandBuffers = &cmdbuf;
        if (vkQueueSubmit(queue, 1, &siA, VK_NULL_HANDLE) != VK_SUCCESS) { fprintf(stderr, "submit RP fail\n"); return 1; }
        vkDeviceWaitIdle(dev);
    }
    {
        VkCommandBuffer cb2; vkAllocateCommandBuffers(dev, &cba, &cb2);
        vkBeginCommandBuffer(cb2, &cbb);
        vkCmdCopyImageToBuffer(cb2, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, buf, 1, &bic);
        vkEndCommandBuffer(cb2);
        VkSubmitInfo siB = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        siB.commandBufferCount = 1; siB.pCommandBuffers = &cb2;
        if (vkQueueSubmit(queue, 1, &siB, VK_NULL_HANDLE) != VK_SUCCESS) { fprintf(stderr, "submit copy fail\n"); return 1; }
        vkDeviceWaitIdle(dev);
    }
    goto done;
readback:
    vkCmdCopyImageToBuffer(cmdbuf, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &bic);
    vkEndCommandBuffer(cmdbuf);

    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmdbuf;
    if (vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) { fprintf(stderr, "submit fail\n"); return 1; }
    vkDeviceWaitIdle(dev);

done:
    vkMapMemory(dev, bmem, 0, VK_WHOLE_SIZE, 0, &data);
    uint8_t *px = (uint8_t*)data + 32 * 256 + 32 * 4;
    uint8_t eR = (uint8_t)(0.1f * 255.0f) ^ 0x22;   // dst.R=0x22
    uint8_t eG = (uint8_t)(0.2f * 255.0f) ^ 0x33;   // dst.G=0x33
    uint8_t eB = (uint8_t)(0.3f * 255.0f) ^ 0x44;   // dst.B=0x44
    uint8_t eA = (uint8_t)(0.4f * 255.0f) ^ 0x11;   // dst.A=0x11
    printf("pixel B=%02x G=%02x R=%02x A=%02x\n", px[0], px[1], px[2], px[3]);
    printf("expect B=%02x G=%02x R=%02x A=%02x (src^dst, XOR logic op)\n", eB, eG, eR, eA);
    printf("RESULT: %s\n",
           (px[0] == eB && px[1] == eG && px[2] == eR && px[3] == eA) ?
           "LOGIC OP EMULATION WORKS END-TO-END" :
           (px[0] == 0x44 && px[1] == 0x33 && px[2] == 0x22 && px[3] == 0x11) ?
               "no fragment write (attachment kept seed)" : "FAILED");
    fflush(stdout);
    vkUnmapMemory(dev, bmem);
    return 0;
}
