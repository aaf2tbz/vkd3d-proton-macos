# 12_0 spike — Metal sparse texture support on Apple GPU (2026-08-14)

Probe: scripts/probes/metal-sparse-probe.m (ObjC, Xcode 27b4 SDK, macOS 27 host)

## Results
- MTLHeapTypeSparse heap (sparsePageSize=MTLSparsePageSize64): created
- Sparse texture (1024x1024 RGBA8 from sparse heap):
    isSparse            = 1
    sparseTextureTier   = 2   (Tier2 = per-tile activity counters)
    firstMipmapInTail   = 1   (tail packing active)
    tile size 2D RGBA8  = 64x64 (16,384 B)
- Sparse buffers: need options refinement (texture path proven; buffer retest later)
- macOS 26+ placement model also present (MTLHeapTypePlacement +
  maxCompatiblePlacementSparsePageSize, MTLSparsePageSize 16/64/256 KB)

## history impact (12_0 rung / Tiled Resources)
- FEASIBLE. Metal sparse tier 1 semantics (partial backing, defined unbacked
  reads: zero / opaque-black, writes discarded) map to D3D12 tiled-resource
  NULL-mapping requirements; tier 2 activity counters map to residency feedback.
- MoltenVK work: expose VkQueueSparseBind / VkSparseImageMemoryBind over
  MTLHeapTypeSparse + per-tile map/unmap; then vkd3d's
  d3d12_device_determine_tiled_resources_tier must see:
  sparseBinding, sparseResidencyAliased, sparseResidencyBuffer,
  sparseResidencyImage2D, residencyStandard2DBlockShape, sparse queue family,
  shaderResourceResidency, shaderResourceMinLod, !residencyAlignedMipSize,
  residencyNonResidentStrict, filterMinmaxSingleComponentFormats (tier1)
  + sparseResidencyImage3D + residencyStandard3DBlockShape (tier2).
- Host GPU tile granularity: 64x64 px (RGBA8), 16 KB — matches D3D12's 64KB
  standard tile expectation after 2x2/4x4 packing per MVK-side mapping.
