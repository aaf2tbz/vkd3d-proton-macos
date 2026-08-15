# M7: DXR ray-query dispatch slice (descriptor support + TLAS + staging)

Status: shader + build + descriptor plumbing COMPLETE; dispatch integration at the
MTL4 argument-table boundary with one known blocker ([[buffer(0)]] collision).

## Landed (probe-verified)
- **AS descriptor support**: VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR mapped
  to the Texture GPU layout (8-byte resource-id slot), OneID CPU layout, the
  VkWriteDescriptorSetAccelerationStructureKHR pNext write path, and Metal3 +
  ArgEncoder argument-buffer writes via setAccelerationStructure (gpuResourceID).
- **TLAS build**: Metal 4 indirect instance descriptors. The Vulkan
  VkAccelerationStructureInstanceKHR array is converted CPU-side (device-address
  -> MVKBuffer lookup) to MTLIndirectAccelerationStructureInstanceDescriptor
  (transform 3x4 row-major -> MTLPackedFloat4x3 columns, mask/userID/iftOffset,
  accelerationStructureReference -> MTLResourceID).
- **SPIRV-Cross MSL fixes** (submodule commit f741ee49):
  1. AS resources index off the msl_texture slot (they are resource-id members).
  2. Ray-query committed getters -> candidate getters: macOS 26 beta returns
     committed distance 0 and committed type always triangle.
  3. Removed the `-1` on OpRayQueryGetIntersectionTypeKHR: Metal's
     intersection_type matches SPIR-V's RayQueryIntersectionTypeKHR.

## macOS 26 beta workarounds (each reproduced in the pure-Metal probe)
| Issue | Symptom | Workaround |
|---|---|---|
| MTL4 AS build can't see CPU-written placement-heap data | BLAS built from stale zeros (0 hits) | stage vertex/index data into standalone shared buffers |
| Heap-backed scratch | AS un-traversable | standalone scratch buffer per build |
| Heap-placed acceleration structures | AS un-traversable | standalone AS allocation (documented deviation: storage outside the VkBuffer) |
| Classic compute encoder can't resolve AS from raw argument data | 0 hits | MTL4 argument-table dispatch for AS pipelines |

## Known blocker (next step)
SPIRV-Cross emits the argument buffer at [[buffer(0)]]; the MTL4 argument table
occupies buffer index 0 itself ("cannot reserve buffer resource location at index
0"). The MTL4 argument-table dispatch therefore cannot bind the descriptor set's
raw data at index 0. Fix: shift the SPIRV-Cross argument-buffer binding to a
non-zero buffer index for MTL4-dispatched pipelines (or bind the argument table
at a different index and remap the kernel's buffer slots).

## Evidence
- vk-as-probe: device+AS build+TLAS build all OK; descriptor write lands the TLAS
  resourceID at slot 1; ray query still returns 0 (blocker above).
- metal-tlas-query-probe (pure Metal): BLAS->instance->TLAS + MTL4 argument table
  + candidate-based intersection_query = 30/256 hits, distances 5.000-5.431
  (exact), proving the whole ray-query data path end to end at the Metal level.
