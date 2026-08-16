/* Native Vulkan capability probe against the custom libMoltenVK.dylib.
 * dlopen's the dylib directly so there is no ICD ambiguity.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

static void *lib;
static PFN_vkGetInstanceProcAddr pvkGIPA;

static void *load_vk(const char *name) {
    void *fn = pvkGIPA(VK_NULL_HANDLE, name);
    if (!fn) { fprintf(stderr, "missing %s\n", name); exit(2); }
    return fn;
}

#define LOADVK(T, name) T name = (T)load_vk(#name)

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1]
        : "extracted/Graphics/dll/moltenvk-vkmt/libMoltenVK.dylib";
    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }
    pvkGIPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    if (!pvkGIPA) { fprintf(stderr, "no vkGetInstanceProcAddr\n"); return 1; }
    printf("loaded %s\n", path);

    uint32_t api_version = 0;
    LOADVK(PFN_vkEnumerateInstanceVersion, vkEnumerateInstanceVersion);
    if (vkEnumerateInstanceVersion(&api_version) == VK_SUCCESS)
        printf("instance api_version : %u.%u.%u\n",
               VK_VERSION_MAJOR(api_version), VK_VERSION_MINOR(api_version),
               VK_VERSION_PATCH(api_version));

    LOADVK(PFN_vkEnumerateInstanceExtensionProperties, vkEnumerateInstanceExtensionProperties);
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    VkExtensionProperties *iext = calloc(n, sizeof *iext);
    vkEnumerateInstanceExtensionProperties(NULL, &n, iext);
    int has_port = 0;
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(iext[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) has_port = 1;
    printf("instance extensions  : %u (portability_enumeration: %s)\n", n, has_port ? "yes" : "no");

    LOADVK(PFN_vkCreateInstance, vkCreateInstance);
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &ai;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ci, NULL, &inst);
    printf("vkCreateInstance     : %d%s\n", vr, has_port ? " (portability bit set)" : "");
    if (vr != VK_SUCCESS) return 1;

    /* device-level entry points resolve through the INSTANCE handle */
    pvkGIPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
#define LOADVK_DEV(T, name) T name = (T)({ \
    void *_fn = pvkGIPA(inst, #name); \
    if (!_fn) { fprintf(stderr, "missing %s\n", #name); return 2; } \
    _fn; })
    LOADVK_DEV(PFN_vkEnumeratePhysicalDevices, vkEnumeratePhysicalDevices);
    uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(inst, &ndev, NULL);
    VkPhysicalDevice *devs = calloc(ndev, sizeof *devs);
    vkEnumeratePhysicalDevices(inst, &ndev, devs);
    printf("physical devices     : %u\n", ndev);
    if (!ndev) return 1;
    VkPhysicalDevice pd = devs[0];

    LOADVK_DEV(PFN_vkGetPhysicalDeviceProperties2, vkGetPhysicalDeviceProperties2);
    VkPhysicalDeviceDriverPropertiesKHR dp = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &dp;
    vkGetPhysicalDeviceProperties2(pd, &p2);
    printf("device               : %s\n", p2.properties.deviceName);
    printf("device api_version   : %u.%u.%u\n",
           VK_VERSION_MAJOR(p2.properties.apiVersion),
           VK_VERSION_MINOR(p2.properties.apiVersion),
           VK_VERSION_PATCH(p2.properties.apiVersion));
    printf("driver_version       : 0x%08x\n", p2.properties.driverVersion);
    printf("driverID             : %d (%s)\n", dp.driverID, dp.driverName);
    printf("vendorID             : 0x%04x deviceID: 0x%04x\n",
           p2.properties.vendorID, p2.properties.deviceID);

    LOADVK_DEV(PFN_vkEnumerateDeviceExtensionProperties, vkEnumerateDeviceExtensionProperties);
    n = 0;
    vkEnumerateDeviceExtensionProperties(pd, NULL, &n, NULL);
    VkExtensionProperties *dext = calloc(n, sizeof *dext);
    vkEnumerateDeviceExtensionProperties(pd, NULL, &n, dext);
    printf("device extensions    : %u\n", n);
    const char *interesting[] = {
        /* feature-level 12_2 families */
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
        VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
        VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME,
        /* feature-level 12_1 families */
        VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,
        VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,
        VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME,
        VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME,
        /* 12_0 / general */
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
        "VK_KHR_maintenance4",
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
        VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME,
        VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME,
    };
    for (size_t k = 0; k < sizeof(interesting)/sizeof(interesting[0]); k++) {
        int found = 0;
        for (uint32_t i = 0; i < n; i++)
            if (!strcmp(dext[i].extensionName, interesting[k])) { found = 1; break; }
        printf("  %-50s %s\n", interesting[k], found ? "PRESENT" : "absent");
    }
    printf("  (full list:)\n");
    for (uint32_t i = 0; i < n; i++)
        printf("    %s v%u\n", dext[i].extensionName, dext[i].specVersion);

    LOADVK_DEV(PFN_vkGetPhysicalDeviceFeatures, vkGetPhysicalDeviceFeatures);
    VkPhysicalDeviceFeatures feat;
    vkGetPhysicalDeviceFeatures(pd, &feat);
    printf("core features        :\n");
    printf("  sparseBinding              : %u\n", feat.sparseBinding);
    printf("  sparseResidencyAliased     : %u\n", feat.sparseResidencyAliased);
    printf("  sparseResidency16Samples   : %u\n", feat.sparseResidency16Samples);
    printf("  shaderInt64                : %u\n", feat.shaderInt64);
    printf("  shaderInt16                : %u\n", feat.shaderInt16);
    printf("  geometryShader             : %u\n", feat.geometryShader);
    printf("  tessellationShader         : %u\n", feat.tessellationShader);
    printf("  shaderStorageImageWriteWithoutFormat: %u\n", feat.shaderStorageImageWriteWithoutFormat);
    printf("  multiDrawIndirect          : %u\n", feat.multiDrawIndirect);
    printf("  shaderTessellationAndGeometryPointSize: %u\n", feat.shaderTessellationAndGeometryPointSize);
    printf("  fragmentStoresAndAtomics   : %u\n", feat.fragmentStoresAndAtomics);
    printf("  logicOp                    : %u\n", feat.logicOp);
    printf("  minTexelBufferOffsetAlignment: %llu\n",
           (unsigned long long)p2.properties.limits.minTexelBufferOffsetAlignment);
    printf("  maxPerStageDescriptorSamplers: %u\n", p2.properties.limits.maxPerStageDescriptorSamplers);
    printf("  maxPerStageDescriptorUniformBuffers: %u\n", p2.properties.limits.maxPerStageDescriptorUniformBuffers);
    printf("  maxPerStageDescriptorStorageBuffers: %u\n", p2.properties.limits.maxPerStageDescriptorStorageBuffers);
    printf("  maxPerStageDescriptorStorageImages: %u\n", p2.properties.limits.maxPerStageDescriptorStorageImages);
    printf("  vertexPipelineStoresAndAtomics : %u\n", feat.vertexPipelineStoresAndAtomics);
    printf("  maxDescriptorSetSamplers   : %u\n", p2.properties.limits.maxDescriptorSetSamplers);

    LOADVK_DEV(PFN_vkGetPhysicalDeviceFeatures2, vkGetPhysicalDeviceFeatures2);
    VkPhysicalDeviceSubgroupProperties sub = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
    VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    sub.pNext = &f11;
    f11.pNext = &f12;
    f12.pNext = &f13;
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.pNext = &sub;
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    printf("subgroup props      : size=%u\n", sub.subgroupSize);
    printf("  supportedOperations: 0x%x\n", sub.supportedOperations);
    printf("  supportedStages    : 0x%x\n", sub.supportedStages);
    printf("vulkan1.1 features  :\n");
    printf("vulkan1.2 features   :\n");
    printf("  bufferDeviceAddress        : %u\n", f12.bufferDeviceAddress);
    printf("  drawIndirectCount          : %u\n", f12.drawIndirectCount);
    printf("  samplerMirrorClampToEdge   : %u\n", f12.samplerMirrorClampToEdge);
    printf("  descriptorIndexing         : %u\n", f12.descriptorIndexing);
    printf("  runtimeDescriptorArray     : %u\n", f12.runtimeDescriptorArray);
    printf("  shaderSampledImageArrayNonUniformIndexing: %u\n", f12.shaderSampledImageArrayNonUniformIndexing);
    printf("  shaderStorageBufferArrayNonUniformIndexing: %u\n", f12.shaderStorageBufferArrayNonUniformIndexing);
    printf("  timelineSemaphore          : %u\n", f12.timelineSemaphore);
    printf("vulkan1.3 features   :\n");
    printf("  maintenance4               : %u\n", f13.maintenance4);
    printf("  dynamicRendering           : %u\n", f13.dynamicRendering);
    printf("  synchronization2           : %u\n", f13.synchronization2);
#ifdef VK_EXT_robustness2
    {
        VkPhysicalDeviceRobustness2FeaturesEXT frb = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
        VkPhysicalDeviceFeatures2 frb2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        frb2.pNext = &frb;
        vkGetPhysicalDeviceFeatures2(pd, &frb2);
        printf("  robustBufferAccess2        : %u\n", frb.robustBufferAccess2);
        printf("  robustImageAccess2         : %u\n", frb.robustImageAccess2);
        printf("  nullDescriptor             : %u\n", frb.nullDescriptor);
    }
#endif

#ifdef VK_KHR_ray_tracing_pipeline
    {
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR frt = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
        VkPhysicalDeviceAccelerationStructureFeaturesKHR fas = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        frt.pNext = &fas;
        VkPhysicalDeviceFeatures2 fr2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        fr2.pNext = &frt;
        vkGetPhysicalDeviceFeatures2(pd, &fr2);
        printf("raytracing features  : pipeline=%u accelerationStructure=%u\n",
               frt.rayTracingPipeline, fas.accelerationStructure);
    }
#endif
#ifdef VK_EXT_mesh_shader
    {
        VkPhysicalDeviceMeshShaderFeaturesEXT fm = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
        VkPhysicalDeviceFeatures2 fm2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        fm2.pNext = &fm;
        vkGetPhysicalDeviceFeatures2(pd, &fm2);
        printf("mesh shader features : taskShader=%u meshShader=%u\n", fm.taskShader, fm.meshShader);
    }
#endif
#ifdef VK_KHR_fragment_shading_rate
    {
        VkPhysicalDeviceFragmentShadingRateFeaturesKHR fv = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
        VkPhysicalDeviceFeatures2 fv2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        fv2.pNext = &fv;
        vkGetPhysicalDeviceFeatures2(pd, &fv2);
        printf("VRS features         : pipelineFragmentShadingRate=%u primitiveFragmentShadingRate=%u attachmentFragmentShadingRate=%u\n",
               fv.pipelineFragmentShadingRate, fv.primitiveFragmentShadingRate, fv.attachmentFragmentShadingRate);
    }
#endif
#ifdef VK_EXT_fragment_shader_interlock
    {
        VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT fi = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT };
        VkPhysicalDeviceFeatures2 fi2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        fi2.pNext = &fi;
        vkGetPhysicalDeviceFeatures2(pd, &fi2);
        printf("fragment interlock   : fragmentShaderSampleInterlock=%u pixelInterlock=%u\n",
               fi.fragmentShaderSampleInterlock, fi.fragmentShaderPixelInterlock);
    }
#endif
    return 0;
}
