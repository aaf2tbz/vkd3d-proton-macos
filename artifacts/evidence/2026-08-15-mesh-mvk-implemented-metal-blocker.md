# Mesh machinery: MVK implementation state + the Metal-side draw blocker (2026-08-15)

## MVK implementation (DONE, committed-work-in-progress)
- `MVKShaderStage` Mesh + Task stages + the VkShaderStageFlagBits <-> stage
  mappings + the SPIRV-Cross execution-model handling (the mesh MSL emission
  exists in the fetched SPIRV-Cross: `emit_mesh_entry_point`, `spvMeshSizes`,
  `mesh<PerV, void, N, M, topology::triangle>`).
- The graphics pipeline accepts the mesh/task stages; a mesh pipeline builds a
  `MTLMeshRenderPipelineDescriptor` (meshFunction + objectFunction + the
  fragment + the color output), compiled via
  `newRenderPipelineStateWithMeshDescriptor:options:reflection:error:`.
- `vkCmdDrawMeshTasksEXT` implemented (the MVKCmdDrawMeshTasks command +
  the entry point + the pool + the encoder state finalization +
  `drawMeshThreadgroups:threadsPerObjectThreadgroup:threadsPerMeshThreadgroup:`
  with the mesh/object threadgroup sizes from the shaders).
- Verified: the mesh pipeline CREATES (VK_SUCCESS, non-nil Metal state) and
  the draw EXECUTES with the correct state.

## The remaining blocker: the Metal-side mesh draw produces no fragments
The native-Vulkan mesh probe (mesh shader emitting the fullscreen triangle,
3 verts / 1 prim, `vkCmdDrawMeshTasksEXT(1,1,1)`) renders NOTHING (red=0/4096).
The same result is reproduced in a PLAIN-METAL baseline (no MVK): a hand-written
`[[mesh]]` function + the fragment + `drawMeshThreadgroups` renders nothing on
the Apple M4 / macOS 26, with:
- mesh-only and object+mesh pipelines (both compile, both draw nothing)
- `drawMeshThreadgroups` and `drawMeshThreads`
- mesh threadgroup sizes 3, 32, 64; cull-mode none; small and fullscreen
  triangles; the Metal API+GPU validation clean (no errors, command buffer
  completes with status completed)
- the device supports the mesh shading (Apple9 family, maxThreadsPerThreadgroup
  1024)

This is a genuine Metal-side mesh behavior issue on this platform that needs
further research (the mesh-function threadgroup/grid semantics, the object
stage's meshgrid-size mechanism, or a macOS 26 mesh-driver quirk). The MVK
machinery is complete; the mesh acceptance gate is gated on resolving the
Metal-side behavior.

## Next steps for the mesh
1. Research the Metal object/mesh draw semantics (the `[[meshgrid_size]]` /
   `maxTotalThreadgroupsPerMeshGrid` mechanism for the object stage; the mesh
   threadgroup requirements).
2. Test the mesh on a different Metal family (if available) to isolate a
   driver/OS quirk.
3. Iterate the plain-Metal baseline until a mesh draw lands pixels, then
   re-run the MVK probe.
