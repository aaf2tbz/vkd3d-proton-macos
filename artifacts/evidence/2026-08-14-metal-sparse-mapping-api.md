# 12_0: Metal per-tile sparse mapping API found (macOS 26) - vkQueueBindSparse target

MTL4CommandQueue.h (macOS 26+, this host):
- MTL4UpdateSparseTextureMappingOperation { mode (Map/Unmap), textureRegion (TILES),
  textureLevel, textureSlice, heapOffset (in tiles) }
- -[MTLCommandQueue updateTextureMappings:texture:heap:operations:count:]
- MTL4UpdateSparseBufferMappingOperation + updateBufferMappings: (sparse buffers)
- Model: placement-sparse textures (MTLTextureDescriptor.placementSparsePageSize,
  firstMipmapInTail/tailSizeInBytes) + MTLHeapTypePlacement heaps with
  maxCompatiblePlacementSparsePageSize; tiles are mapped/unmapped per-operation.
- MTLTextureSparseTier semantics (MTLResource.h): tier1 = unmapped reads return
  zero (A=0) or opaque black (A=1), writes dropped; tier2 = + per-tile activity
  counters. Matches D3D12 tiled-resource NULL-tile semantics.

=> vkQueueBindSparse mapping is 1:1:
  VkSparseImageMemoryBind {subresource,offset,extent,memory,memoryOffset,flags}
    -> MTL4UpdateSparseTextureMappingOperation (pixel->tile conversion via
       sparseTileSizeWithTextureType / convertSparsePixelRegions)
  VkSparseBufferMemoryBind -> updateBufferMappings
  Sparse DEVICE_LOCAL memory -> MTLHeapTypePlacement heap (page size match)

Open items for honest advertisement:
- shaderResourceResidency / shaderResourceMinLod: Metal has no shader-side
  residency query (only tier-2 activity counters) -> vkd3d tier gate must be
  relaxed for MoltenVK (D3D12 tiled resources have NO shader residency query;
  documented deviation like the SM 6.2 case).
- residencyStandard2DBlockShape: Metal standard per-format tile shapes (probe:
  64x64 RGBA8, 16KB) -> TRUE
- residencyAlignedMipSize: Metal packs mips into the tail (firstMipmapInTail>0)
  -> FALSE (matches vkd3d tier-1 requirement)
- residencyNonResidentStrict: Metal defines unmapped-access results -> TRUE
