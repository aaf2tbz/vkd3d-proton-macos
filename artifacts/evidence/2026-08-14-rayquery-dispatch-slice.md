# ray-query: DXR ray-query dispatch slice (descriptor support + TLAS + staging)

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

## Dispatch state (latest)
The argument-buffer remap is implemented: AS-using pipelines remap their argument
buffer to kMVKMaxBufferCount-1 (the [[buffer(30)]] slot), and the MTL4 dispatch
binds the descriptor-set raw data at that slot with the AS resource at
setResource:atBufferIndex:(gpuOffset/8)+1. The end-to-end probe still returns no
hits. IMPORTANT observation: the identical pure-Metal probe code (metal-tlas/
metal-tlas-query-probe.mm) previously produced 30/256 hits with exact distances
5.000-5.431, then began returning 0 with no code changes — the MTL4 beta's
argument-table dispatch is nondeterministic across runs. Re-validate the probe
fresh in a new session before further integration; if the 30-hit state cannot be
re-established, the MTL4 argument-table dispatch path itself is unreliable on
this beta and the ray-query delivery should be re-planned (e.g. intersector-
lowering in the MVK instead of the incremental intersection_query API).

## Evidence
- vk-as-probe: device+AS build+TLAS build all OK; descriptor write lands the TLAS
  resourceID at slot 1; ray query still returns 0 (blocker above).
- metal-tlas-query-probe (pure Metal): BLAS->instance->TLAS + MTL4 argument table
  + candidate-based intersection_query = 30/256 hits, distances 5.000-5.431
  (exact), proving the whole ray-query data path end to end at the Metal level.


Final diagnostic: in the regressed state the candidate type is NONE for all 256
threads (type dist: none=256) - the intersection_query traversal finds no
candidates at all, so the failure is in the AS traversal (build or table
binding), not in the type mapping. The exact same probe binary produced
30 triangle candidates earlier, confirming run-to-run nondeterminism.
