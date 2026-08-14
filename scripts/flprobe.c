/* D3D12 feature-level probe for the custom vkd3d-proton stack.
 * Uses OFFICIAL Microsoft D3D12_FEATURE numeric ids (mingw renumbers them).
 * Run under the installed MetalSharp Wine 11.5 with the tar's d3d12.dll,
 * d3d12core.dll (vkd3d-proton) and dxgi.dll (DXVK) staged next to this exe.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define COBJMACROS
#include <initguid.h>
#include <dxgi1_6.h>
#include <d3d12.h>

/* Official D3D12_FEATURE ids (Microsoft ABI; do NOT use mingw enum names) */
#define FEAT_OPTIONS         ((D3D12_FEATURE)0)
#define FEAT_ARCHITECTURE    ((D3D12_FEATURE)1)
#define FEAT_FEATURE_LEVELS  ((D3D12_FEATURE)2)
#define FEAT_SHADER_MODEL    ((D3D12_FEATURE)18)
#define FEAT_OPTIONS5        ((D3D12_FEATURE)19) /* raytracing tier */
#define FEAT_OPTIONS6        ((D3D12_FEATURE)20) /* VRS */
#define FEAT_OPTIONS7        ((D3D12_FEATURE)21) /* mesh shader, sampler feedback */

#define FL_1_0_CORE 0x1000 /* D3D_FEATURE_LEVEL_1_0_CORE */

/* NOTE: D3D12_RAYTRACING_TIER and D3D12_FEATURE_DATA_D3D12_OPTIONS5 are provided
 * by the mingw d3d12.h with the official layouts. Do NOT redefine them.
 * mingw's D3D12_FEATURE enum VALUES are renumbered, hence FEAT_* above. */

static const char *hr_hex(HRESULT hr) {
    static char buf[24];
    snprintf(buf, sizeof buf, "0x%08lx", (unsigned long)hr);
    return buf;
}

static const char *fl_name(D3D_FEATURE_LEVEL l) {
    switch (l) {
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    case D3D_FEATURE_LEVEL_9_3:  return "9_3";
    case D3D_FEATURE_LEVEL_9_2:  return "9_2";
    case D3D_FEATURE_LEVEL_9_1:  return "9_1";
    case FL_1_0_CORE:            return "1_0_CORE";
    default:                     return "?";
    }
}

static void module_identity(const char *name) {
    HMODULE h = GetModuleHandleA(name);
    char path[MAX_PATH];
    if (!h) { printf("  module %-14s : NOT LOADED\n", name); return; }
    if (!GetModuleFileNameA(h, path, sizeof path)) { printf("  module %-14s : loaded (path unknown)\n", name); return; }
    printf("  module %-14s : %s\n", name, path);
}

int main(void) {
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    ID3D12Device *dev = NULL;
    HRESULT hr;
    UINT i;

    printf("=== loaded module identity ===\n");
    module_identity("d3d12.dll");
    module_identity("d3d12core.dll");
    module_identity("dxgi.dll");

    /* adapter enumeration via staged DXVK dxgi */
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    printf("\n=== adapter enumeration (CreateDXGIFactory1: %s) ===\n", hr_hex(hr));
    if (SUCCEEDED(hr)) {
        for (i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) == S_OK; i++) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->lpVtbl->GetDesc1(adapter, &desc);
            printf("  adapter %u: vendor 0x%04x device 0x%04x \"%ls\"\n",
                   i, desc.VendorId, desc.DeviceId, desc.Description);
        }
        if (i == 0) printf("  (no adapters enumerated)\n");
    }

    /* device creation per minimum feature level */
    printf("\n=== D3D12CreateDevice per minimum feature level ===\n");
    {
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, FL_1_0_CORE
        };
        for (i = 0; i < sizeof(levels)/sizeof(levels[0]); i++) {
            dev = NULL;
            hr = D3D12CreateDevice((IUnknown *)adapter, levels[i], &IID_ID3D12Device, (void **)&dev);
            printf("  min %-8s : hr=%s dev=%s", fl_name(levels[i]), hr_hex(hr), dev ? "CREATED" : "NULL");
            if (FAILED(hr) || !dev) {
                /* NULL-adapter path: D3DKMT default adapter */
                dev = NULL;
                HRESULT hr2 = D3D12CreateDevice(NULL, levels[i], &IID_ID3D12Device, (void **)&dev);
                printf("  | NULL-adapter: hr=%s dev=%s", hr_hex(hr2), dev ? "CREATED" : "NULL");
            }
            printf("\n");
            if (dev) dev->lpVtbl->Release(dev);
            dev = NULL;
        }
    }

    /* full query set on a device created at 11_0 */
    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    if (FAILED(hr) || !dev) {
        hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
        if (SUCCEEDED(hr) && dev)
            printf("\n(queries run on device created via NULL adapter)\n");
    } else {
        printf("\n(queries run on device created via enumerated adapter)\n");
    }
    if (FAILED(hr) || !dev) {
        printf("\nCould not create device at 11_0 for feature queries: %s\n", hr_hex(hr));
        return 1;
    }
    printf("\n=== CheckFeatureSupport on device created at 11_0 ===\n");

    {
        D3D_FEATURE_LEVEL requested[] = {
            D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, FL_1_0_CORE
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl = { 6, requested, 0 };
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_FEATURE_LEVELS, &fl, sizeof fl);
        printf("  FEATURE_LEVELS      : hr=%s max=%s (0x%04x)\n",
               hr_hex(hr), fl_name(fl.MaxSupportedFeatureLevel), (unsigned)fl.MaxSupportedFeatureLevel);
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS o;
        memset(&o, 0, sizeof o);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS, &o, sizeof o);
        printf("  OPTIONS             : hr=%s\n", hr_hex(hr));
        if (SUCCEEDED(hr)) {
            printf("    ResourceBindingTier   : %u (1=%u,2=%u,3=%u)\n",
                   o.ResourceBindingTier,
                   D3D12_RESOURCE_BINDING_TIER_1, D3D12_RESOURCE_BINDING_TIER_2,
                   D3D12_RESOURCE_BINDING_TIER_3);
            printf("    TiledResourcesTier    : %u (not=%u,t1=%u,t2=%u,t3=%u)\n",
                   o.TiledResourcesTier,
                   D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED, D3D12_TILED_RESOURCES_TIER_1,
                   D3D12_TILED_RESOURCES_TIER_2, D3D12_TILED_RESOURCES_TIER_3);
            printf("    ConservativeRasterTier: %u (not=%u,t1=%u,t2=%u,t3=%u)\n",
                   o.ConservativeRasterizationTier,
                   D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED,
                   D3D12_CONSERVATIVE_RASTERIZATION_TIER_1,
                   D3D12_CONSERVATIVE_RASTERIZATION_TIER_2,
                   D3D12_CONSERVATIVE_RASTERIZATION_TIER_3);
            printf("    ROVsSupported         : %u\n", o.ROVsSupported);
            printf("    OutputMergerLogicOp   : %u (11_1 logical blend ops)\n", o.OutputMergerLogicOp);
            printf("    VPAndRTArrayIndexAnyShader (TIR): %u\n",
                   o.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation);
            printf("    MaxGPUVirtualAddressBitsPerResource: %u\n", o.MaxGPUVirtualAddressBitsPerResource);
            printf("    ResourceHeapTier      : %u\n", o.ResourceHeapTier);
            printf("    CrossNodeSharingTier  : %u\n", o.CrossNodeSharingTier);
            printf("    PSSpecifiedStencilRef : %u\n", o.PSSpecifiedStencilRefSupported);
        }
    }

    {
        D3D12_FEATURE_DATA_SHADER_MODEL sm;
        memset(&sm, 0, sizeof sm);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_SHADER_MODEL, &sm, sizeof sm);
        printf("  SHADER_MODEL        : hr=%s highest=0x%x\n", hr_hex(hr), (unsigned)sm.HighestShaderModel);
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5;
        memset(&o5, 0, sizeof o5);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS5, &o5, sizeof o5);
        printf("  OPTIONS5 (DXR)      : hr=%s RaytracingTier=%u (not=%u,t1_0=%u,t1_1=%u)\n",
               hr_hex(hr), o5.RaytracingTier,
               D3D12_RAYTRACING_TIER_NOT_SUPPORTED, D3D12_RAYTRACING_TIER_1_0,
               D3D12_RAYTRACING_TIER_1_1);
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6;
        memset(&o6, 0, sizeof o6);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS6, &o6, sizeof o6);
        /* official values: not=0, tier1=10, tier2=20 (mingw renumbers the enum) */
        printf("  OPTIONS6 (VRS)      : hr=%s tier=%u (0=not,10=t1,20=t2) tileSize=%u\n",
               hr_hex(hr), o6.VariableShadingRateTier, o6.ShadingRateImageTileSize);
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7;
        memset(&o7, 0, sizeof o7);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS7, &o7, sizeof o7);
        printf("  OPTIONS7 (mesh)     : hr=%s MeshShaderTier=%u (not=%u,t1=%u) SamplerFeedbackTier=%u\n",
               hr_hex(hr), o7.MeshShaderTier,
               D3D12_MESH_SHADER_TIER_NOT_SUPPORTED, D3D12_MESH_SHADER_TIER_1,
               o7.SamplerFeedbackTier);
    }

    {
        D3D12_FEATURE_DATA_ARCHITECTURE a;
        memset(&a, 0, sizeof a);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_ARCHITECTURE, &a, sizeof a);
        printf("  ARCHITECTURE        : hr=%s UMA=%u CacheCoherentUMA=%u TileBasedRenderer=%u\n",
               hr_hex(hr), a.UMA, a.CacheCoherentUMA, a.TileBasedRenderer);
    }

    dev->lpVtbl->Release(dev);
    if (adapter) adapter->lpVtbl->Release(adapter);
    if (factory) factory->lpVtbl->Release(factory);
    printf("\nprobe done\n");
    return 0;
}
