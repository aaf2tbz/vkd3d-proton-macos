# M5 SPARSE EXECUTION LANDED - vkQueueBindSparse implemented (2026-08-14)

Status: **EXECUTED** (was: advertisement-only). Full sparse cycle proven on GPU.
The 12_0 rung now has a real execution path for tiled resources.

## What was implemented (MoltenVK fork)

- `MVKImage`: stores `_createFlags`; `getIsSparse()`; `getSparseMemoryRequirements()`
  (tile size via `sparseTileSizeWithTextureType:pixelFormat:sampleCount:`,
  `firstMipmapInTail`, `tailSizeInBytes`); sparse image creation sets
  `placementSparsePageSize = MTLSparsePageSize64` and **forces
  `MTLTextureUsageShaderWrite`** (see tier-1 discovery below).
- `vkGetImageSparseMemoryRequirements` in vulkan.mm returns per-aspect plane
  requirements (verified: granularity 64x64, tailFirstLod=1).
- `MVKDeviceMemory::ensureMTLHeap()`: `MTLHeapTypePlacement` heaps with
  `maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64`; sparse
  DEVICE_LOCAL memory allocations are heap-backed.
- `MVKQueue::bindSparse()`: lazy `getMTL4CommandQueue()` (macOS 26 MTL4 queue);
  image binds -> `updateTextureMappings:heap:operations:count:` (one call per
  bind, region in tiles, `heapOffset = memoryOffset / sparseTileSizeInBytes`);
  unmap binds -> mode UNMAP with nil heap; timeline semaphore waits/signals via
  MTL4 `waitForEvent`/`signalEvent` on MTLSharedEvent; fence via
  `MVKFence::signal()`.
- Ordering: bindSparse signals a per-queue `_mtl4MappingEvent` on the MTL4
  queue; the next classic-queue command buffer (from `getMTLCommandBuffer`)
  encodes `encodeWaitForEvent` before any work, so GPU work sees the mappings.
  (MTL4CommandEncoder barrier API is unimplemented on this GPU - MTLEvent sync
  is the reliable ordering primitive.)

## Critical discovery: sparse TIER selection

The texture usage bitmask selects the Metal sparse tier:

- usage = ShaderRead|RenderTarget (MVK's mapping for COLOR_ATTACHMENT +
  TRANSFER_SRC|DST) -> **tier 2** placement-sparse texture. MTL4
  `updateTextureMappings` completes without error, but ANY GPU write
  (blit copy, vkCmdClearColorImage) faults with
  `kIOGPUCommandBufferCallbackErrorPageFault` (code 3), and the device is
  lost. Reproduced in a pure-Metal probe with the exact MVK descriptor
  (tier 2) - deterministic failure; identical descriptor with ShaderWrite ->
  tier 1 - deterministic success.
- usage with ShaderWrite -> **tier 1** placement-sparse texture; mappings
  work, writes land, readbacks are pixel-exact.

Fix: sparse textures in MVKImage::newMTLTextureDescriptor() get
`mtlTexDesc.usage |= MTLTextureUsageShaderWrite;` -> tier 1. (Tier 2 on
Apple9/M4/macOS 26 is effectively broken for placement sparse - documented
deviation, tier-1 semantics are a superset of what D3D12 tiled resources
need: NULL-tile reads return 0, writes dropped.)

## Probe evidence (Vulkan level, self-built libMoltenVK 1.4.3)

/tmp/vk-sparse-probe (Vulkan, no loader): 256x256 B8G8R8A8 UNORM image with
SPARSE_BINDING|SPARSE_RESIDENCY, 64KB-page heap (256 KB DEVICE_LOCAL),
16 tile binds (heap offsets 0..15):

- sparse image create OK; requirements: 1 aspect plane, granularity 64x64,
  tailFirstLod=1 tailSize=0
- bindSparse OK; 5/5 deterministic runs:
  `mapped sparse tile readback: 0x11223344` (write 0x11223344 via
  vkCmdCopyBufferToImage -> barrier -> vkCmdCopyImageToBuffer readback)
- unmap tile (0,0) via bindSparse with VK_NULL_HANDLE memory -> readback of
  that tile returns 0x00000000: **D3D12 NULL-tile semantics confirmed**
  (`UNMAP RESULT: NULL-TILE SEMANTICS CONFIRMED`)

Pure-Metal validation (scripts/probes/metal-sparse-map-probe.mm, mvk4exact
pattern): event-synced blit write/readback through the same MTL4 mapping path
yields 0x11223344 deterministically (10/10).

## Full-stack regression (self-built stack under Wine, flprobe)

Fresh vkd3d-proton build from current source (relaxation commits included -
the previous stage pair predated commit 6e67b7e and its tier gate variant
differed):

```
min 12_0     : hr=0x00000000 dev=CREATED
min 12_1     : hr=0x80070057 dev=NULL   (next rung, not yet implemented)
FEATURE_LEVELS      : max=12_0 (0xc000)
TiledResourcesTier  : 4
OutputMergerLogicOp : 1
SHADER_MODEL        : highest=0x66 (SM 6.6)
```

## Residual gaps (documented, no overclaim)

- vkGetBufferSparseMemoryRequirements + buffer sparse binds: buffer binds
  recorded (resourceOffset) but not yet exercised end-to-end; D3D12 tiled
  resources use buffer binds for raw buffers.
- Tail mip packing (alignedMipSize=FALSE honest): tail reads/writes land in
  the tail block; not yet exercised on GPU (tail is mip1+ here, size 0 for
  the probe's single mip).
- MTL4 queue only exists on macOS 26 + Apple GPU; older configs fail
  bindSparse with VK_ERROR_FEATURE_NOT_PRESENT (MVK getConfigurationResult).
- Debug prints removed; code builds clean (make macos, build59).

## Artifacts

- Probes: /tmp/vk-sparse-probe.c (Vulkan), scripts/probes/metal-sparse-map-probe.mm
- Builds: artifacts/mvk-build49..59.log
- Ladder: artifacts/evidence/rung-12_0-run2.txt (fresh vkd3d + new MVK)
