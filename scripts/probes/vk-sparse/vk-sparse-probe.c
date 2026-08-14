#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <dlfcn.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
static void* lib;
static PFN_vkGetInstanceProcAddr gipa;
static VkInstance inst; static VkPhysicalDevice pd; static VkDevice dev; static VkQueue q;
#define LD(T,name) T name=(T)gipa(inst,#name)
#define LDR(T,name) T name=(T)gipa(inst,#name); if(!name){fprintf(stderr,"missing %s\n",#name);return 1;}
static uint8_t* read_spv(const char* p, size_t* n){FILE*f=fopen(p,"rb");if(!f){perror(p);exit(1);}fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);uint8_t*b=malloc(sz);if(fread(b,1,sz,f)!=(size_t)sz){perror("fread");exit(1);}fclose(f);*n=sz;return b;}
int main(int argc,char**argv){
    const char* dylib=argc>1?argv[1]:"libMoltenVK.dylib";
    lib=dlopen(dylib,RTLD_NOW); if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
    gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vkGetInstanceProcAddr");
    VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo=&ai;
    LDR(PFN_vkCreateInstance,vkCreateInstance); vkCreateInstance(&ici,NULL,&inst);
    LDR(PFN_vkEnumeratePhysicalDevices,vkEnumeratePhysicalDevices); uint32_t n=1; vkEnumeratePhysicalDevices(inst,&n,&pd);
    LDR(PFN_vkGetPhysicalDeviceQueueFamilyProperties,vkGetPhysicalDeviceQueueFamilyProperties);
    uint32_t qfc=0; vkGetPhysicalDeviceQueueFamilyProperties(pd,&qfc,NULL);
    VkQueueFamilyProperties* qfp=calloc(qfc,sizeof(*qfp)); vkGetPhysicalDeviceQueueFamilyProperties(pd,&qfc,qfp);
    uint32_t qf=0; for(uint32_t i=0;i<qfc;i++) if(qfp[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){qf=i;break;}
    float prio=1.0f; VkDeviceQueueCreateInfo dq={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    dq.queueFamilyIndex=qf; dq.queueCount=1; dq.pQueuePriorities=&prio;
    VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&dq;
    LDR(PFN_vkCreateDevice,vkCreateDevice); if(vkCreateDevice(pd,&dci,NULL,&dev)!=VK_SUCCESS){fprintf(stderr,"dev fail\n");return 1;}
    LDR(PFN_vkGetDeviceQueue,vkGetDeviceQueue); vkGetDeviceQueue(dev,qf,0,&q);
    LDR(PFN_vkGetPhysicalDeviceMemoryProperties,vkGetPhysicalDeviceMemoryProperties);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd,&mp);
    uint32_t devT=UINT32_MAX,hostT=UINT32_MAX;
    for(uint32_t i=0;i<mp.memoryTypeCount;i++){
        if(devT==UINT32_MAX && (mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))devT=i;
        if((mp.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))hostT=i;
    }
    fprintf(stderr, "mem types: devT=%u hostT=%u count=%u\n", devT, hostT, mp.memoryTypeCount);
    // 1) sparse image creation
    LDR(PFN_vkCreateImage,vkCreateImage);
    VkImageCreateInfo ic={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.flags=VK_IMAGE_CREATE_SPARSE_BINDING_BIT|VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
    ic.imageType=VK_IMAGE_TYPE_2D; ic.format=VK_FORMAT_B8G8R8A8_UNORM;
    ic.extent=(VkExtent3D){256,256,1}; ic.mipLevels=1; ic.arrayLayers=1;
    ic.samples=VK_SAMPLE_COUNT_1_BIT; ic.tiling=VK_IMAGE_TILING_OPTIMAL;
    ic.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ic.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img; VkResult vr=vkCreateImage(dev,&ic,NULL,&img);
    fprintf(stderr, "sparse image create: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL", (int)vr);
    if(vr!=VK_SUCCESS) return 1;
    // 2) sparse memory requirements
    LDR(PFN_vkGetImageSparseMemoryRequirements,vkGetImageSparseMemoryRequirements);
    uint32_t nreq=0; vkGetImageSparseMemoryRequirements(dev,img,&nreq,NULL);
    fprintf(stderr, "sparse reqs count: %u\n", nreq);
    VkSparseImageMemoryRequirements* reqs=calloc(nreq?nreq:1,sizeof(*reqs));
    vkGetImageSparseMemoryRequirements(dev,img,&nreq,reqs);
    if(nreq) fprintf(stderr, "  tailFirstLod=%u tailSize=%llu granularity=%lux%lu\n",
        reqs[0].imageMipTailFirstLod,(unsigned long long)reqs[0].imageMipTailSize,
        (unsigned long)reqs[0].formatProperties.imageGranularity.width,
        (unsigned long)reqs[0].formatProperties.imageGranularity.height);
    // 3) memory: full binding (1 tile row of 4 tiles: 256x256 / 64x64 = 4x4 tiles = 16 tiles)
    LDR(PFN_vkAllocateMemory,vkAllocateMemory);
    VkMemoryAllocateInfo mai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize=64*64*4*16; mai.memoryTypeIndex=devT;   // 16 tiles x 16KB
    VkDeviceMemory mem; if(vkAllocateMemory(dev,&mai,NULL,&mem)!=VK_SUCCESS){fprintf(stderr,"mem alloc fail\n");return 1;}
    // 4) bind sparse: map all 16 tiles
    LDR(PFN_vkQueueBindSparse,vkQueueBindSparse);
    VkSparseImageMemoryBind binds[16]; uint32_t nb=0;
    uint32_t tileW=64,tileH=64;
    for(uint32_t ty=0;ty<4;ty++) for(uint32_t tx=0;tx<4;tx++){
        binds[nb].subresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        binds[nb].subresource.mipLevel=0; binds[nb].subresource.arrayLayer=0;
        binds[nb].offset=(VkOffset3D){tx*tileW,ty*tileH,0};
        binds[nb].extent=(VkExtent3D){tileW,tileH,1};
        binds[nb].memory=mem; binds[nb].memoryOffset=(VkDeviceSize)(getenv("PAGE_OFF") ? (ty*4+tx)*4*tileW*tileH*4 : (ty*4+tx)*tileW*tileH*4);
        binds[nb].flags=0; nb++;
    }
    VkSparseImageMemoryBindInfo binfo={img,nb,binds};
    VkBindSparseInfo bsi={VK_STRUCTURE_TYPE_BIND_SPARSE_INFO};
    bsi.imageBindCount=1; bsi.pImageBinds=&binfo;
    vr=vkQueueBindSparse(q,1,&bsi,VK_NULL_HANDLE);
    fprintf(stderr, "bindSparse: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
    { PFN_vkDeviceWaitIdle dwi=(PFN_vkDeviceWaitIdle)gipa(inst,"vkDeviceWaitIdle"); if(dwi) dwi(dev); }
    if (getenv("SLEEP")) { usleep(300000); }
    // 5) write via transfer and read back
    LDR(PFN_vkCreateBuffer,vkCreateBuffer); LDR(PFN_vkGetBufferMemoryRequirements,vkGetBufferMemoryRequirements);
    LDR(PFN_vkBindBufferMemory,vkBindBufferMemory); LDR(PFN_vkMapMemory,vkMapMemory); LDR(PFN_vkUnmapMemory,vkUnmapMemory);
    VkBufferCreateInfo bc={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size=256*256*4; bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf; vkCreateBuffer(dev,&bc,NULL,&buf);
    VkMemoryRequirements bmr; vkGetBufferMemoryRequirements(dev,buf,&bmr);
    VkMemoryAllocateInfo bai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; bai.allocationSize=bmr.size; bai.memoryTypeIndex=hostT;
    VkDeviceMemory bmem; vkAllocateMemory(dev,&bai,NULL,&bmem); vkBindBufferMemory(dev,buf,bmem,0);
    void* data; vkMapMemory(dev,bmem,0,VK_WHOLE_SIZE,0,&data);
    uint32_t val=0x11223344; for(int i=0;i<256*256;i++) ((uint32_t*)data)[i]=val;
    vkUnmapMemory(dev,bmem);
    LDR(PFN_vkCreateCommandPool,vkCreateCommandPool); LDR(PFN_vkAllocateCommandBuffers,vkAllocateCommandBuffers);
    LDR(PFN_vkBeginCommandBuffer,vkBeginCommandBuffer); LDR(PFN_vkEndCommandBuffer,vkEndCommandBuffer);
    LDR(PFN_vkCmdPipelineBarrier2,vkCmdPipelineBarrier2); LDR(PFN_vkCmdCopyBufferToImage,vkCmdCopyBufferToImage);
    LDR(PFN_vkCmdCopyImageToBuffer,vkCmdCopyImageToBuffer);
    LDR(PFN_vkQueueSubmit,vkQueueSubmit); LDR(PFN_vkDeviceWaitIdle,vkDeviceWaitIdle);
    VkCommandPoolCreateInfo cpc={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpc.queueFamilyIndex=qf;
    VkCommandPool pool; vkCreateCommandPool(dev,&cpc,NULL,&pool);
    VkCommandBufferAllocateInfo cba={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cba.commandPool=pool; cba.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cba.commandBufferCount=1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(dev,&cba,&cb);
    VkCommandBufferBeginInfo cbb={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb,&cbb);
    VkImageMemoryBarrier2 ib={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask=VK_PIPELINE_STAGE_2_NONE; ib.srcAccessMask=0;
    ib.dstStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT; ib.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
    ib.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ib.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    ib.image=img; ib.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkDependencyInfo dep={VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dep.imageMemoryBarrierCount=1; dep.pImageMemoryBarriers=&ib;
    vkCmdPipelineBarrier2(cb,&dep);
    VkBufferImageCopy bic={0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{256,256,1}};
    if (getenv("USE_CLEAR")) {
        LDR(PFN_vkCmdClearColorImage,vkCmdClearColorImage);
        VkClearColorValue cc = {{ 0.07f, 0.2f, 0.27f, 0.4f }};  // ~ 0x11223344-ish
        VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdClearColorImage(cb,img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&cc,1,&range);
    } else {
        vkCmdCopyBufferToImage(cb,buf,img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&bic);
    }
    ib.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT; ib.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
    ib.dstStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT; ib.dstAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;
    ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier2(cb,&dep);
    vkCmdCopyImageToBuffer(cb,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buf,1,&bic);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&cb;
    vr=vkQueueSubmit(q,1,&si,VK_NULL_HANDLE);
    { PFN_vkDeviceWaitIdle dwi=(PFN_vkDeviceWaitIdle)gipa(inst,"vkDeviceWaitIdle"); if(dwi) dwi(dev); }
    vkMapMemory(dev,bmem,0,VK_WHOLE_SIZE,0,&data);
    uint32_t px=((uint32_t*)data)[32*256+32];
    fprintf(stderr, "mapped sparse tile readback: 0x%08x (expect 0x11223344)\n", px);
    fprintf(stderr, "RESULT: %s\n", px==0x11223344 ? "SPARSE IMAGE + BIND + WRITE + READBACK WORKS" : "FAILED");
    if (getenv("TEST_UNMAP")) {
        // Unmap tile (0,0) -> its reads must return ZERO (D3D12 NULL-tile semantics)
        VkSparseImageMemoryBind ub;
        ub.subresource = binds[0].subresource;
        ub.offset = binds[0].offset; ub.extent = binds[0].extent;
        ub.memory = VK_NULL_HANDLE; ub.memoryOffset = 0; ub.flags = 0;
        VkSparseImageMemoryBindInfo ubi; ubi.image = img; ubi.bindCount = 1; ubi.pBinds = &ub;
        VkBindSparseInfo usi; memset(&usi,0,sizeof(usi));
        usi.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO; usi.imageBindCount = 1; usi.pImageBinds = &ubi;
        vr = vkQueueBindSparse(q,1,&usi,VK_NULL_HANDLE);
        fprintf(stderr, "unmap: %s (%d)\n", vr==VK_SUCCESS?"OK":"FAIL",(int)vr);
        { PFN_vkDeviceWaitIdle dwi=(PFN_vkDeviceWaitIdle)gipa(inst,"vkDeviceWaitIdle"); if(dwi) dwi(dev); }
        // Re-record the readback (fresh cb state)
        vkBeginCommandBuffer(cb,&cbb);
        ib.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ib.srcStageMask=VK_PIPELINE_STAGE_2_NONE; ib.srcAccessMask=0;
        ib.dstStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT; ib.dstAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier2(cb,&dep);
        vkCmdCopyImageToBuffer(cb,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buf,1,&bic);
        vkEndCommandBuffer(cb);
        vr=vkQueueSubmit(q,1,&si,VK_NULL_HANDLE);
        { PFN_vkDeviceWaitIdle dwi=(PFN_vkDeviceWaitIdle)gipa(inst,"vkDeviceWaitIdle"); if(dwi) dwi(dev); }
        vkMapMemory(dev,bmem,0,VK_WHOLE_SIZE,0,&data);
        uint32_t px2=((uint32_t*)data)[32*256+32];
        fprintf(stderr, "unmapped tile readback: 0x%08x (expect 0x00000000 NULL-tile semantics)\n", px2);
        fprintf(stderr, "UNMAP RESULT: %s\n", px2==0 ? "NULL-TILE SEMANTICS CONFIRMED" : "FAILED");
        vkUnmapMemory(dev,bmem);
    }
    fflush(stdout);
    return 0;
}
