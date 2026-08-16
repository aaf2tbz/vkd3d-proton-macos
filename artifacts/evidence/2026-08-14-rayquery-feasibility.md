# ray-query: INLINE RAY QUERY FEASIBILITY - GREEN on Apple GPU (2026-08-14)

Status: the single largest 12_2 risk (4A.4) is retired at the Metal API level:
the inline ray/geometry intersection (the VK_KHR_ray_query analog) works on
GPU. The remaining work is the SPIRV-Cross MSL lowering of the RayQuery ops
to the intersector (an implementation task, not a feasibility risk).

## The prototype

scripts/probes/metal-rayquery-probe.mm:

1. BLAS: one triangle (0,0,-5),(4,0,-5),(0,4,-5) via
   MTL4PrimitiveAccelerationStructureDescriptor +
   MTL4AccelerationStructureTriangleGeometryDescriptor, built on the MTL4
   compute encoder (`buildAccelerationStructure:descriptor:scratchBuffer:`).
2. Compute kernel (`kernel void raycast`): a `metal::raytracing::intersector`
   in a plain COMPUTE function (the inline mode - no ray-tracing pipeline!):
   `intersector<triangle_data> i; ray r(origin, dir, 0, 1000); auto hit = i.intersect(r, accel);`
   - exactly the RayQuery use case (per-thread inline intersection).
3. Ray grid 16x16 from (2,2,0) toward -z; hit distances written to a buffer.

Result: 30 hits forming the triangle's projection; minD=5.000 maxD=5.431
(the triangle at z=-5 - the distances are EXACT).

## MTL4 API notes (macOS 26)

- Command flow: `[dev newMTL4CommandQueueWithDescriptor:error:]`,
  `[dev newCommandAllocator]`, `[dev newCommandBuffer]`,
  `[cb beginCommandBufferWithAllocator:]`, encoders, `[cb endCommandBuffer]`,
  `[q4 commit:&cb count:1 options:opts]`; completion via
  `MTL4CommitOptions addFeedbackHandler:` (no waitUntilCompleted on MTL4).
- Resource binding: `MTL4ArgumentTable` (maxBufferBindCount), buffers via
  `setAddress:atIndex:`, acceleration structures via `setResource:atBufferIndex:`
  with `gpuResourceID`.
- `MTL4BufferRange` takes the buffer's GPU ADDRESS, not the buffer object.
- MTL4InstanceAccelerationStructureDescriptor has NO instanced-BLAS array
  property (the classic API has it) - the probe uses the BLAS directly
  (non-instancing intersector).

## What this unblocks

- `VK_KHR_ray_query` -> MSL: the intersector in compute/fragment functions
  (SPIRV-Cross MSL currently lacks the RayQuery op emission; that lowering is
  the next implementation item).
- `VK_KHR_acceleration_structure`: the BLAS/TLAS build paths (MTL4
  acceleration structure API) - the DXR 1.1 (4A.2/4A.3) groundwork.
- D3D12 inline RayQuery (vkd3d-proton: dxil-spirv emits VK_KHR_ray_query
  SPIR-V for the DXIL RayQuery intrinsics).

## Evidence

- scripts/probes/metal-rayquery-probe.mm (the prototype)
- Run: device Apple GPU; BLAS 640B; hits=30 minD=5.000 maxD=5.431;
  RESULT: INLINE RAY QUERY (INTERSECTOR) WORKS ON GPU
