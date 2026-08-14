# M5: full sparse cycle PROVEN on Apple M4 (2026-08-14)

Probe: scripts/probes/metal-sparse-map-probe.mm
1. Placement-sparse texture (MTLTextureDescriptor.placementSparsePageSize=64KB,
   storageMode=Private): isSparse=1, sparseTextureTier=1
2. MTLHeapTypePlacement heap (maxCompatiblePlacementSparsePageSize=64KB)
3. MTL4 command queue (newMTL4CommandQueueWithDescriptor:)
4. updateTextureMappings (Map): region in TILES (64x64 RGBA8 tiles), heapOffset
   in tiles
5. GPU write to a mapped tile (classic compute, event-synced after the MTL4
   queue signal): 0.101961 0.200000 0.301961 0.400000  <- LANDED
6. updateTextureMappings (Unmap) + GPU read: 0 0 0 0  <- NULL-TILE SEMANTICS

Sync pattern: MTL4 queue signals an MTLEvent after the mapping; the classic
command buffer encodeWaitForEvent:value: before the GPU work. (The MTL4
CommandEncoder barrier API is not implemented on this GPU's compute context.)

=> vkQueueBindSparse mapping is fully implementable:
   - VkSparseImageMemoryBind -> updateTextureMappings (tile regions)
   - sparse memory (DEVICE_LOCAL) -> MTLHeapTypePlacement heap
   - vkQueueBindSparse ordering -> MTL4 queue + MTLEvent sync
   - unmapped-tile reads return 0 (A=0) / opaque-black (A=1) - D3D12 NULL tiles
