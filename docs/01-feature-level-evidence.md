# Phase 0 — Feature-Level Evidence Lock (2026-08-14)

> This file is the historical Phase 0 baseline. It is retained for
> provenance; see [features.md](features.md) and
> `artifacts/evidence/rung-ladder-2026-08-16.txt` for the current M14 result.

Source artifact: `/Users/averyfelts/Desktop/metalsharp-graphics-dll-clean.tar.zst` (120,978,648 bytes, extracted with zstd).
Runtime: installed MetalSharp Wine 11.5 (`/Users/averyfelts/.metalsharp/runtime/wine`), Rosetta x86_64.
Host: Apple M4 (vendor 0x106b, device 0x1b000209), MSL 4.0, GPU Family Metal 4 / Apple 9.

## 1. Shipped binaries (identity)

| File | Size | SHA-256 |
|---|---|---|
| `Graphics/dll/vkd3d-proton/x86_64-windows/d3d12.dll` | 446,464 | `7a34f49a8cf309e20df8f5418c133d8e6a00882155de5532eef2bd9b9f094f93` |
| `Graphics/dll/vkd3d-proton/x86_64-windows/d3d12core.dll` | 6,434,816 | `8b643bfbdc9acab92aee8c76ce971b9877f0b851cf6fe2aa04bc37cca5ac22e4` |
| `Graphics/dll/moltenvk-vkmt/libMoltenVK.dylib` | 11,009,664 (universal x86_64+arm64) | `50e41de23ce85260870c24cec11ac29b225704c6cb0366ce555dcd9ac03417f3` |
| `Graphics/dll/dxvk/x86_64-windows/dxgi.dll` | 18,940,247 | (DXVK DXGI provider; not part of the feature-level question) |

- d3d12.dll: PE32+ x86-64; exports `D3D12CreateDevice`, `D3D12GetInterface`, `D3D12GetDebugInterface`, root-signature serialization, `D3D12EnableExperimentalFeatures`. Forwarder for d3d12core.dll (`CLSID_VKD3DCore`).
- d3d12core.dll: PE32+ x86-64; exports **only** `D3D12GetInterface` + `D3D12SDKVersion` (Agility-style split). Runtime log: `vkd3d-proton - applicationVersion: 3.1.0`, `build: 3300fe64cc1ecf5+`, built with `clang 22.1.8 (…ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)` — identical to llvm-mingw 20260616's compiler.
- Custom markers: `stream output is disabled (VKMT)`, `D3DKMTOpenAdapterFromLuid` import, `d3d12core_CreateDeviceFromFactory`, `ID3D12DXVKInteropDevice{,1,2,3}`, `DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE`, `MoltenVK 0.2.2210`-style device label.
- MoltenVK_icd.json: `library_path: "./libMoltenVK.dylib"`, `api_version 1.4.0`, `is_portability_driver: true`.

## 2. MoltenVK native capability dump (dlopen probe against the exact dylib)

```
MoltenVK 1.4.2, Vulkan instance 1.4.357 / device 1.3.357, driverID 14 (MoltenVK)
154 instance extensions, 130 device extensions
core:    sparseBinding 0 · sparseResidencyAliased 0 · geometryShader 0 · tessellationShader 1
         logicOp 0 · shaderInt64 1 · shaderInt16 1 · multiDrawIndirect 1
         vertexPipelineStoresAndAtomics 1 · fragmentStoresAndAtomics 1
         shaderStorageImageWriteWithoutFormat 1
1.2:     bufferDeviceAddress 1 · drawIndirectCount 0 · descriptorIndexing 1
         runtimeDescriptorArray 1 · timelineSemaphore 1 · samplerMirrorClampToEdge 1
1.3:     maintenance4 1 · dynamicRendering 1 · synchronization2 1
robust2: robustBufferAccess2 1 · robustImageAccess2 1 · nullDescriptor 1
RT:      rayTracingPipeline 0 · accelerationStructure 0
mesh:    taskShader 0 · meshShader 0
VRS:     pipeline/primitive/attachmentFragmentShadingRate 0 0 0
interlock: fragmentShaderSampleInterlock 1 · fragmentShaderPixelInterlock 1
limits:  minTexelBufferOffsetAlignment 16
         maxPerStageDescriptorStorageBuffers 31   (< 64 = D3D12_UAV_SLOT_COUNT)
         maxPerStageDescriptorStorageImages 256
extension matrix (relevant):
  PRESENT:   VK_KHR_buffer_device_address, VK_EXT_buffer_device_address,
             VK_EXT_fragment_shader_interlock, VK_EXT_shader_viewport_index_layer,
             VK_KHR_maintenance4..9, VK_KHR_robustness2, VK_KHR_push_descriptor,
             VK_KHR_fragment_shader_barycentric, VK_NV_fragment_shader_barycentric,
             VK_EXT_descriptor_indexing, VK_EXT_sampler_filter_minmax,
             VK_KHR_deferred_host_operations v4, VK_KHR_timeline_semaphore,
             VK_EXT_shader_atomic_float, VK_KHR_swapchain_maintenance1, VK_EXT_scalar_block_layout
  ABSENT:    VK_EXT_conservative_rasterization, VK_KHR_ray_tracing_pipeline,
             VK_KHR_acceleration_structure, VK_KHR_ray_query, VK_EXT_mesh_shader,
             VK_KHR_fragment_shading_rate, VK_EXT_transform_feedback,
             VK_KHR_shader_atomic_int64, VK_KHR_compute_shader_derivatives,
             VK_KHR_draw_indirect_count, VK_EXT_mutable_descriptor_type,
             VK_KHR_portability_enumeration (instance)
```

## 3. vkd3d-proton feature-level ladder (authoritative source)

`libs/vkd3d/device.c::d3d12_device_caps_init_feature_level()` — byte-comparable logic in upstream v3.0.1 and master:

```
max = 11_0
→ 11_1 if OutputMergerLogicOp && vertexPipelineStoresAndAtomics
        && maxPerStageDescriptorStorageBuffers >= 64
        && maxPerStageDescriptorStorageImages  >= 64
→ 12_0 if (≥11_1) && TiledResourcesTier ≥ 2 && ResourceBindingTier ≥ 2
        && TypedUAVLoadAdditionalFormats
→ 12_1 if (≥12_0) && ROVsSupported && ConservativeRasterizationTier ≥ 1
→ 12_2 if (≥12_1) && max_shader_model ≥ 6.5 && TIR && WaveOps && Int64ShaderOps
        && DepthBoundsTestSupported && CopyQueueTimestampQueriesSupported
        && CastingFullyTypedFormatSupported && RB tier ≥ 3 && CR tier ≥ 3
        && Tiled tier ≥ 3 && RaytracingTier ≥ 1.1 && VRS tier ≥ 2
        && MeshShaderTier ≥ 1 && SamplerFeedbackTier ≥ 0.9
```

Option gating (same file):
- `OutputMergerLogicOp = VkPhysicalDeviceFeatures.logicOp`
- `ROVsSupported = fragmentShaderPixelInterlock`
- `ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3` (hardcoded)
- Tiled tier: sparse chain — `sparseBinding, sparseResidencyAliased, sparseResidencyBuffer, sparseResidencyImage2D, residencyStandard2DBlockShape, sparse queue family, shaderResourceResidency, shaderResourceMinLod, !residencyAlignedMipSize, residencyNonResidentStrict, filterMinmaxSingleComponentFormats` → tier 1; `+ sparseResidencyImage3D + residencyStandard3DBlockShape` → tier 2; else higher.
- Conservative raster tier: `VK_EXT_conservative_rasterization` → tier 1; `+ degenerateTrianglesRasterized` → tier 2; `+ fullyCoveredFragmentShaderInputVariable` → tier 3; absent → NOT_SUPPORTED.
- Raytracing tier: RT Vulkan extensions + VBO format features (+ inline ray query for 1.1).
- Mesh tier: `meshShader && taskShader` (VK_EXT_mesh_shader).
- VRS tier: VK_KHR_fragment_shading_rate support chain.
- Sampler feedback tier: `shaderInt64 && shaderImageInt64Atomics`.
- SM ladder: 6.0 (subgroup compute+fragment ops, scalar/UBO-standard layout, shaderInt16) → 6.2 (denorm float controls) → 6.3 (SPIR-V 1.4) → 6.5 → 6.6 (needs compute derivatives or NVIDIA driverID — **blocked on this MVK**).

## 4. Wine-side live captures (installed Wine 11.5, staged DLLs, ICD pinned)

1. Loader identity: `d3d12.dll` → `Z:\tmp\flprobe\stage\d3d12.dll` (native), `d3d12core.dll` loaded native; winevulkan created the instance through the pinned ICD (MoltenVK banner).
2. vkd3d device-caps ran per attempt: `Lacking transform-feedback behavior; stream output is disabled (VKMT)`, `Lacking support for single texel alignment`, `VK_EXT_dynamic_rendering_unused_attachments not supported`.
3. `D3D12CreateDevice(adapter|NULL, 0x1000)` → `warn:vkd3d-proton:vkd3d_create_device: Invalid feature level 0x1000.` → `E_INVALIDARG`. **CORE_1_0 rejected by the D3D12 side (empirical).**
4. All levels ≥ 11_1 returned `E_INVALIDARG` in the bare-wine env **before** the feature-level check (custom core's adapter/D3DKMT path) — the per-rung create HRESULTs must be re-proven in the repaired env (M1). This does not change the ceiling verdict: the MoltenVK-measured caps fail ladder rung 1 (`logicOp=0`), so max = 11_0 regardless.

## 5. Verdict (locked)

| Level | Verdict | Blocking evidence |
|---|---|---|
| 11_0 | **SUPPORTED** | ladder baseline; SM 6.5 ≥ 5.1; RB tier 3; Control runs on this pair |
| 11_1 | NOT SUPPORTED | `logicOp=0`; storage buffers 31/64 |
| 12_0 | NOT SUPPORTED | `sparseBinding=0` → tiled NOT_SUPPORTED |
| 12_1 | NOT SUPPORTED | no `VK_EXT_conservative_rasterization` |
| 12_2 | NOT SUPPORTED | no RT/mesh/VRS Vulkan surface |
| CORE_1_0 | NOT SUPPORTED | live `Invalid feature level 0x1000` |

`VKD3D_FEATURE_LEVEL` env override exists in the shipped code (force-sets option bits) — banned as support evidence.
