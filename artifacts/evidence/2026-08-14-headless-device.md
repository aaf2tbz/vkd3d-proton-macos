# headless/feature-level Evidence — Headless device creation + full feature queries (2026-08-14)

## headless blocker found & fixed
Root cause: upstream vkd3d-proton `device.c:3317-3321` treats missing single-texel
alignment as a HARD failure:
    if (!single_storage_texel || !single_uniform_texel) { ERR("Lacking support for
    single texel alignment.\n"); return E_INVALIDARG; }
MoltenVK reports minTexelBufferOffsetAlignment=16 → every D3D12CreateDevice returned
E_INVALIDARG (silent from the app's view; the ERR is the last log line).

Fix: the MetalSharp `metalsharp-wine` wrapper sets the VKMT escape hatch:
    VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT=1   (+ MVK_PRESENT_MODE=1)
With it, device creation succeeds. The env is part of the real launch shape:
    DYLD_FALLBACK_LIBRARY_PATH=$MS_LIB/moltenvk-vkmt:$MS_LIB:$MS_LIB/wine/x86_64-unix
    VK_ICD_FILENAMES=$MS_ROOT/etc/vulkan/icd.d/MoltenVK_icd.json   (absolute library_path)

## Empirical results (shipped pair: d3d12 7a34f49a / d3d12core 8b643bfb / MVK 50e41de2)
D3D12CreateDevice per minimum feature level (adapter from DXVK dxgi, NULL fallback):
    12_2, 12_1, 12_0, 11_1 → E_INVALIDARG (0x80070057)
    11_0 → S_OK (device created)
    1_0_CORE → E_INVALIDARG ("Invalid feature level 0x1000")

CheckFeatureSupport (device at 11_0):
    FEATURE_LEVELS: MaxSupportedFeatureLevel = 11_0 (0xb000)          ← LADDER PROVEN
    OPTIONS: ResourceBindingTier=3, TiledResourcesTier=NOT_SUPPORTED,
             ConservativeRasterizationTier=NOT_SUPPORTED, ROVsSupported=1,
             OutputMergerLogicOp=0, TIR(VPAndRTArrayIndexAnyShader)=1,
             MaxGPUVirtualAddressBitsPerResource=40, ResourceHeapTier=2,
             PSSpecifiedStencilRef=1, CrossNodeSharingTier=0
    SHADER_MODEL (mingw id 7): S_OK, HighestShaderModel=0x0  (custom build returns 0;
             official id 18 → E_INVALIDARG)
    OPTIONS5 (mingw id 27): RaytracingTier = NOT_SUPPORTED
    OPTIONS6 (mingw id 30): VariableShadingRateTier = NOT_SUPPORTED
    OPTIONS7 (mingw id 32): MeshShaderTier = NOT_SUPPORTED, SamplerFeedbackTier = NOT_SUPPORTED
    ARCHITECTURE: UMA=0 CacheCoherentUMA=0 TileBasedRenderer=0 (custom build returns 0s)

## NEW ABI FINDING: custom build uses mingw-renumbered D3D12_FEATURE enum
Official ids (SHADER_MODEL=18, OPTIONS5=19, OPTIONS6=20, OPTIONS7=21) → E_INVALIDARG.
Mingw ids (7, 27, 30, 32) → S_OK. The VKMT build was compiled with mingw-w64 headers
which renumber D3D12_FEATURE. Our rebuilt custom vkd3d-proton must match this ABI
(or deliberately fix it and document the break).

## OTHER OBSERVATIONS
- caps init logs: "Not all relevant pipeline stages are supported by EXT_dgc. Skipping."
  "Topology: UMA-like topology", "HVV usage allowed", "Device does not support
  VK_EXT_mutable_descriptor_type (or VALVE)"
- [mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with
  format VK_FORMAT_R8_UINT, which does not support it. (vkd3d internal meta passes;
  non-fatal)
- fixme:vkd3d-proton:d3d12_device_caps_init_feature_options1: TotalLaneCount = 1024
- d3d12_find_physical_device: "Could not find Vulkan physical device for DXGI adapter."
  (fixme level) when the DXGI adapter LUID cannot be matched to a Vulkan device —
  occurs with wine's builtin dxgi; DXVK's adapter path matches.
