# 12_1 3.1: ROV EXECUTION VERIFIED - interlock ordering works (2026-08-14)

Status: the 12_1 rung's ROV execution item is DONE at the Vulkan level.

## What was proven

scripts/probes/vk-rov/vk-rov-probe.c: two fullscreen draws of the same
triangle with different ids (1 then 2), each fragment shader writing its id
to a R32G32B32A32_UINT UAV inside `beginInvocationInterlockARB()/
endInvocationInterlockARB()` (SPV_EXT_fragment_shader_interlock,
OpExecutionMode PixelInterlockOrderedEXT).

- Both draws: ALL 4096 UAV pixels = 2 (the second draw's write wins
  everywhere - the writes are ORDERED per-pixel).
- First-draw-only control: all pixels = 1.

## Findings along the way

1. **SPIRV-Cross needs the interlock EXECUTION MODE**: the raster_order_group
   MSL qualifier is only emitted when the entry point declares
   `OpExecutionMode PixelInterlockOrderedEXT` (or the Unordered variant).
   Without it the interlocked-resource analysis is skipped and the texture
   gets no raster_order_group -> no actual ordering.
2. The MSL output: `texture2d<uint, access::write> [[id(0), raster_order_group(0)]]`
   + the write inside the interlock region.
3. glslang does not support GL_EXT_fragment_shader_interlock; the probe
   assembles the SPIR-V by hand (rov-fs-id1/id2.spvasm in the repo) with
   spirv-as.
4. The SPIR-V entry point interface must list ALL globals (UAV included).
5. A Vulkan probe pitfall: pipelines with NULL static viewports need the
   dynamic viewport state enabled for vkCmdSetViewport to take effect
   (same bug fixed in the CR probe earlier).

## Stack

MVK: fragment shader interlock advertised from `_metalFeatures.rasterOrderGroups`
(Apple GPUs: areRasterOrderGroupsSupported). The D3D12 ROV path (vkd3d
shader translation of RasterizerOrdered* UAVs to the interlock SPIR-V) is the
remaining D3D12-level verification; the Vulkan-level execution contract
(per-pixel ordered UAV writes) is now proven.
