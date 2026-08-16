# Closing Gaps Plan: Mesh pipelines + Sampler feedback (2026-08-15)

No ship until both slices are DONE with runtime acceptance. The feature-level
gates are green and the D3D12 graphics-draw path is fixed (MVK 13e3c96); the
two remaining execution-machinery slices are:

1. **Slice 1 — Mesh/task pipeline machinery** (the MVK side; the vkd3d side is
   already complete).
2. **Slice 3 — Sampler-feedback shader path** (the 64-bit image-atomic
   lowering; the exact failure point is unconfirmed yet).

Grounding facts (verified in the tree):
- vkd3d: `DispatchMesh` → `vkCmdDrawMeshTasksEXT` (command.c:21576), mesh PSO
  stages (`VK_SHADER_STAGE_MESH_BIT_EXT|TASK_BIT_EXT`, state.c:1988-1997),
  mesh root-signature push stages, mesh io-signature validation — READY.
- MVK: NO mesh/task shader stages (no `kMVKShaderStageMesh/Task`), NO
  `vkCmdDrawMeshTasksEXT` entry, NO `meshFunction`/`objectFunction` in the
  pipeline descriptor. The meshShader/taskShader feature bits are reported
  true (the gate relaxation).
- SPIRV-Cross (the fetched tree the MVK builds): the MSL mesh emission EXISTS
  (`emit_mesh_entry_point`, `spvMeshSizes`, `MeshGridProperties`) — the
  shader-translation side is available.
- Sampler feedback: the vkd3d reports the 0.9 tier; the Metal rejects
  `atomic_ulong` image atomics, so `VK_EXT_shader_image_atomic_int64` cannot
  be exposed; the current WriteSamplerFeedback shader path is unverified
  (whether it fails in the vkd3d-shader or in the MVK compile).

---

## Slice 1 — Mesh/task pipeline machinery (MVK)

### M1.1 MVKShaderStage plumbing (API/mvk_datatypes.h + MVKShaderModule.mm)
- Add `kMVKShaderStageMesh` + `kMVKShaderStageTask` to `MVKShaderStage`.
- Map `VK_SHADER_STAGE_MESH_BIT_EXT`/`TASK_BIT_EXT` → the new stages.
- Map the SPIRV-Cross execution models (`ExecutionModelMeshEXT`/`TaskEXT`)
  → the new stages in the shader-conversion config.
- Size the stage-resource arrays (`kMVKShaderStageCount` + the resource
  bindings arrays) and the barrier stage bits for the two new stages.
- Acceptance: the mesh/task MSL conversion runs without asserts
  (SPIRV-Cross emits the mesh entry point — already present).

### M1.2 Graphics pipeline: mesh/task stage acceptance (MVKPipeline.mm)
- `vkCreateGraphicsPipelines` currently rejects unknown stages; accept
  `VkPipelineShaderStageCreateInfo` with the mesh/task stages:
  - the per-stage shader-library conversion via the existing
    `getShaderLibrary` path (the SPIRV-Cross MSL, mesh stage);
  - the stage-resource binding + the bindScript for the mesh/task stages
    (the vertex-bindings bypass: mesh shaders have no vertex inputs);
  - `MTLRenderPipelineDescriptor.meshFunction` + `.objectFunction`
    (object = task stage) + `maxTotalThreadsPerObjectThreadgroup` +
    `maxTotalThreadsPerMeshThreadgroup` (from the MSL's
    `[[max_total_threads_per_threadgroup]]` / the SPIRV-Cross's
    `spvMeshSizes` constants).
- Keep the existing vertex/fragment path untouched (the D3D12 graphics path
  is green — regression check after every change).
- Acceptance: a native-Vulkan mesh pipeline (mesh stage SPIR-V assembled from
  the SPIRV-Cross emission) creates; the pipeline creation is
  `VK_SUCCESS` and the Metal pipeline state is non-nil.

### M1.3 vkCmdDrawMeshTasksEXT (+ indirect) (Vulkan/vulkan.mm + MVKCmdDraw)
- Add the `vkCmdDrawMeshTasksEXT` entry: `drawMeshThreadgroups:
  threadsPerObjectThreadgroup: threadsPerMeshThreadgroup:` with the
  threadgroup sizes from the pipeline (the mesh/object threadgroup sizes);
  the encoder state for the mesh stage (the pipeline + the resources, no
  vertex buffers).
- Add `vkCmdDrawMeshTasksIndirectEXT` after the direct form is green.
- Acceptance: the native-Vulkan mesh probe: a mesh shader emitting a
  position+color payload via `SetMeshOutputsEXT`-equivalent → the render
  target readback pixel-exact (a known color pattern per vertex).

### M1.4 D3D12 acceptance probe (the roadmap gate)
- `scripts/probes/mesh/mesh_probe.c` + the ms_6_5 shaders (dxc):
  - mesh shader with the meshlet (position + color payload),
    `SetMeshOutputsEXT`, per-primitive exports;
  - the D3D12 pipeline with the MS (mesh) + optional AS (amplification)
    stages; `DispatchMesh` → the render-target readback pixel-exact;
  - the mesh-output-count verification (the payload size readback);
  - the amplification shader (task) variant: the AS writes the threadgroup
    counts → the MS dispatch matches.
- Run under wine with the staged pair + the real MoltenVK; the readback
  must match the CPU reference exactly (the same pattern approach as the
  InnerCoverage probe).

### Risks / fallbacks (recorded, not blockers)
- Metal mesh payload limits (the `maxTotalThreadsPerMeshThreadgroup` +
  payload-size limits) vs the D3D12 expectations — the probe defines the
  practical ceiling; the probe stays within the limits.
- SPIRV-Cross emission gaps for the per-primitive outputs / the payload
  structs — fix in the SPIRV-Cross patch (the same patch mechanism as the
  FullyCoveredEXT patch, `scripts/patch-spirv-cross.sh`).
- If the task/object stage hits a Metal limitation, the mesh-only path
  (no amplification stage) is the acceptance; the task stage is a separate
  sub-item with its own probe.

---

## Slice 3 — Sampler-feedback shader path (64-bit image-atomic lowering)

### S3.1 Ground truth (the first step — 1h, decides the whole approach)
- Write the sampler-feedback probe shader (dxc, SM 6.5):
  `WriteSamplerFeedback(tex, samp, coord, 0, mip, layer)` +
  `SampleCmpLevelZero` — compile to DXIL.
- Run it through the vkd3d (VKD3D_SHADER_DUMP_PATH) and capture:
  (a) does the vkd3d-shader compile the WriteSamplerFeedback DXIL at all?
  (b) the exact SPIR-V ops on the feedback resource (the 64-bit
  OpAtomicIAdd? the layout of the two 32-bit words?);
  (c) where the current failure occurs: the vkd3d-shader conversion, the
  MVK SPIRV-Cross, or the Metal compile.
- The output determines whether the lowering belongs in:
  (a) the vkd3d-shader (the DXIL/SPIR-V generation — the app's
  WriteSamplerFeedback → 32-bit ops directly), or
  (b) the MVK SPIRV-Cross (a 64-bit-image-atomic → 32-bit CAS-loop MSL
  lowering), or
  (c) a combination.

### S3.2 The emulation (per the evidence: vkd3d-side, 32-bit encoding)
- The feedback texel is 64-bit (the D3D12 feedback format: the mip encoding
  + the written flag). Metal CAN do 32-bit image atomics.
- The lowering: the 64-bit atomic on the feedback UAV → a two-word 32-bit
  compare-exchange loop:
  - read the 64-bit texel as two 32-bit words (two 32-bit image loads);
  - compute the new value (the mip/written encoding);
  - `OpAtomicCompareExchange` on the FIRST word with the CAS-retry loop
    (the second word updates once the first CAS succeeds, or a combined
    two-word scheme chosen from the S3.1 ground truth);
  - the EXACT bit layout must match what `ReadSamplerFeedback` /
    `DecodeSamplerFeedback` expect on the read side (the vkd3d controls
    both directions — the ABI is internal to the vkd3d path).
- The reads stay 64-bit loads (Metal supports them) — no change needed on
  the decode side unless the layout forces it.

### S3.3 D3D12 acceptance probe (the roadmap gate)
- `scripts/probes/feedback/feedback_probe.c` + the dxc shader:
  the feedback resource (R64_UINT, the D3D12 feedback view), a dispatch
  with the WriteSamplerFeedback over a known region, the readback of the
  feedback resource, and the CPU reference: the expected mip levels +
  the written-flag counts per texel.
- The readback must match the CPU reference exactly.
- Regression: the CORE_1_0 corpus + the ladder stay green.

### Risks / fallbacks (recorded)
- The CAS-loop ordering/consistency: the feedback writes are relaxed by
  nature (a "has been sampled" hint) — the D3D12 spec tolerates
  last-writer-wins; the loop only needs the two words to be consistent.
- If the layout is vkd3d-specific, the decode side of the vkd3d's
  meta-shaders (if any) must use the same encoding — verified by the
  probe's readback.
- If the 64-bit atomic appears in a form the CAS scheme cannot express
  exactly (e.g., the atomic max on the mip), choose the encoding variant
  from S3.1 and document it.

---

## Sequencing + gates
1. S3.1 first (1h, cheap) — it determines the feedback approach.
2. M1.1 → M1.2 → M1.3 (native-Vulkan mesh probe green) → M1.4 (D3D12 mesh
   probe green). The D3D12 graphics path is the regression net after every
   MVK pipeline change (`cr_inner_probe` + the ladder).
3. S3.2 → S3.3 (D3D12 feedback probe green).
4. Final: refresh the M14 artifact (the d3d12 pair + libMoltenVK with the
   mesh + feedback machinery), re-run the full probe suite (ladder, corpus,
   DXR, InnerCoverage, mesh, feedback), update the roadmap statuses, and
   only then ship.
