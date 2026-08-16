/* D3D12 feature-level probe for the custom vkd3d-proton stack.
 * Uses OFFICIAL Microsoft D3D12_FEATURE numeric ids (mingw renumbers them).
 * Run under a compatible Wine installation with the package's d3d12.dll,
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
#define FEAT_SHADER_MODEL    ((D3D12_FEATURE)18) /* official */
#define FEAT_OPTIONS5        ((D3D12_FEATURE)19) /* official; raytracing tier */
#define FEAT_OPTIONS6        ((D3D12_FEATURE)20) /* official; VRS */
#define FEAT_OPTIONS7        ((D3D12_FEATURE)21) /* official; mesh shader */
/* The CUSTOM VKMT build was compiled with mingw-w64 headers, which RENUMBER the
 * D3D12_FEATURE enum. Its feature switch uses the mingw values:
 *   SHADER_MODEL=7, OPTIONS5=27, OPTIONS6=30, OPTIONS7=32
 * Empirically official ids 18-21 return E_INVALIDARG on the shipped pair
 * (2026-08-14, evidence m1-runD.txt). */
#define FEAT_SHADER_MODEL_MINGW ((D3D12_FEATURE)7)
#define FEAT_OPTIONS2_MINGW     ((D3D12_FEATURE)18)
#define FEAT_OPTIONS3_MINGW     ((D3D12_FEATURE)21)
#define FEAT_OPTIONS5_MINGW     ((D3D12_FEATURE)27)
#define FEAT_OPTIONS6_MINGW     ((D3D12_FEATURE)30)
#define FEAT_OPTIONS7_MINGW     ((D3D12_FEATURE)32)

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
        /* NOTE: HighestShaderModel is IN-OUT (app requests max, driver returns min). */
        D3D12_FEATURE_DATA_SHADER_MODEL sm;
        memset(&sm, 0, sizeof sm);
        sm.HighestShaderModel = D3D_SHADER_MODEL_6_6;
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_SHADER_MODEL, &sm, sizeof sm);
        printf("  SHADER_MODEL        : hr=%s (official id 18) highest=0x%x\n", hr_hex(hr), (unsigned)sm.HighestShaderModel);
        if (FAILED(hr)) {
            memset(&sm, 0, sizeof sm);
            sm.HighestShaderModel = D3D_SHADER_MODEL_6_6;
            hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_SHADER_MODEL_MINGW, &sm, sizeof sm);
            printf("    -> mingw id 7      : hr=%s highest=0x%x\n", hr_hex(hr), (unsigned)sm.HighestShaderModel);
        }
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS2 o2;
        memset(&o2, 0, sizeof o2);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS2_MINGW, &o2, sizeof o2);
        printf("  OPTIONS2            : hr=%s DepthBoundsTestSupported=%u ProgrammableSamplePositionsTier=%u\n",
               hr_hex(hr), o2.DepthBoundsTestSupported, o2.ProgrammableSamplePositionsTier);
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 o3;
        memset(&o3, 0, sizeof o3);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS3_MINGW, &o3, sizeof o3);
        printf("  OPTIONS3            : hr=%s CopyQueueTimestampQueries=%u CastingFullyTyped=%u Barycentrics=%u\n",
               hr_hex(hr), o3.CopyQueueTimestampQueriesSupported, o3.CastingFullyTypedFormatSupported, o3.BarycentricsSupported);
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5;
        memset(&o5, 0, sizeof o5);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS5, &o5, sizeof o5);
        printf("  OPTIONS5 (DXR)      : hr=%s (official id 19) RaytracingTier=%u\n", hr_hex(hr), o5.RaytracingTier);
        if (FAILED(hr)) {
            memset(&o5, 0, sizeof o5);
            hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS5_MINGW, &o5, sizeof o5);
            printf("    -> mingw id 27     : hr=%s RaytracingTier=%u (0=not,10=t1_0,11=t1_1)\n", hr_hex(hr), o5.RaytracingTier);
        }
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6;
        memset(&o6, 0, sizeof o6);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS6, &o6, sizeof o6);
        printf("  OPTIONS6 (VRS)      : hr=%s (official id 20) tier=%u\n", hr_hex(hr), o6.VariableShadingRateTier);
        if (FAILED(hr)) {
            memset(&o6, 0, sizeof o6);
            hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS6_MINGW, &o6, sizeof o6);
            printf("    -> mingw id 30     : hr=%s tier=%u (0=not,10=t1,20=t2) tileSize=%u\n",
                   hr_hex(hr), o6.VariableShadingRateTier, o6.ShadingRateImageTileSize);
        }
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7;
        memset(&o7, 0, sizeof o7);
        hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS7, &o7, sizeof o7);
        printf("  OPTIONS7 (mesh)     : hr=%s (official id 21) MeshShaderTier=%u SamplerFeedbackTier=%u\n",
               hr_hex(hr), o7.MeshShaderTier, o7.SamplerFeedbackTier);
        if (FAILED(hr)) {
            memset(&o7, 0, sizeof o7);
            hr = dev->lpVtbl->CheckFeatureSupport(dev, FEAT_OPTIONS7_MINGW, &o7, sizeof o7);
            printf("    -> mingw id 32     : hr=%s MeshShaderTier=%u (0=not,10=t1) SamplerFeedbackTier=%u\n",
                   hr_hex(hr), o7.MeshShaderTier, o7.SamplerFeedbackTier);
        }
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
