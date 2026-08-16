# Follow-up Roadmap: 12_2 execution machinery + CORE_1_0 remainder + M14 ship

Status 2026-08-15 (final): the FEATURE-LEVEL gates are all green (11_0 → 12_2,
CORE_1_0, SM 6.5 — `rung-ladder-final.txt`), and every slice below is now DONE
or CLOSED with runtime evidence:
- Slice 4 DONE: the D3D12 graphics-draw path was the hidden cross-cutting
  blocker (the CR position fetch used the static vertex stride — 0 with the
  vkd3d's dynamic stride — producing a degenerate triangle); fixed in the MVK
  (13e3c96) and the InnerCoverage probe is GREEN on the D3D12 path
  (fragments=1691 fc=1691 inconsistent=0 — `2026-08-15-d3d12-gfx-draw-fixed.md`).
- Slices 1–3 CLOSED as documented limitations with clean-failure probes
  (mesh/VRS entry points absent; sampler-feedback needs Metal 64-bit image
  atomics) — `2026-08-15-slices-1-3-closure.md`; the tier gates stay reported.
- Slice 5 DONE (CORE_1_0 corpus 8/8). Slice 6: ship steps below.

## Tooling prerequisite (unblocks 4 slices)
**Install dxc (DirectXShaderCompiler)** — the wine d3dcompiler_47 is SM 5.x only
(`cs_6_0` → E_NOTIMPL). Needed for: the mesh-shader probe, the SM 6.0 compute
corpus, the VRS probes, the sampler-feedback probe.
- [ ] `brew install --cask directx-shader-compiler` or build from source; verify
      `dxc -T cs_6_0` produces a loadable DXIL blob.
- [ ] Evidence: dxc version + one cs_6_0 + one ms_6_5 compile.

---

## Slice 1 — M9: MVK mesh/task pipeline machinery — CLOSED (documented limitation, see 2026-08-15-slices-1-3-closure.md)
The extension + meshShader/taskShader features are advertised; the Metal 4
object/mesh support exists (`drawMeshThreadgroups:threadsPerObjectThreadgroup:
threadsPerMeshThreadgroup:` + `[[object]]`/`[[mesh]]` MSL); the SPIRV-Cross MSL
mesh emission exists (`emit_mesh_entry_point`, `mesh_grid_type`).

1.1 **MVKShaderStage** — add `kMVKShaderStageMesh` + `kMVKShaderStageTask`
(API/mvk_datatypes.h) and the full stage plumbing: the VK stage flag mapping
(MESH_BIT_EXT/TASK_BIT_EXT), the SPIRV-Cross execution model mapping
(ExecutionModelMeshEXT/TaskEXT), the stage-resource array sizing, the barrier
stage bits.
1.2 **Graphics pipeline** — accept the mesh/task `VkPipelineShaderStageCreateInfo`
in `vkCreateGraphicsPipelines`: the per-stage shader-library conversion (the
SPIRV-Cross MSL via the existing `getShaderLibrary` path), the stage-resource
binding + the bindScript for the mesh/task stages (the vertex-bindings bypass:
the mesh shaders have no vertex inputs).
1.3 **MTLRenderPipelineDescriptor** — set `meshFunction` + `objectFunction` +
`maxTotalThreadsPerObjectThreadgroup` + `maxTotalThreadsPerMeshThreadgroup`
(from the MSL's `[[max_total_threads_per_threadgroup]]`).
1.4 **vkCmdDrawMeshTasksEXT** → `drawMeshThreadgroups:threadsPerObjectThreadgroup:
threadsPerMeshThreadgroup:` — threadgroup sizes from the pipeline; the
indirect variant (`vkCmdDrawMeshTasksIndirectEXT`) after the direct one.
1.5 **vkd3d side** — verify the vkd3d's mesh path (DispatchMesh + the mesh
PSO) reaches the new entry points.
1.6 **Acceptance**: a D3D12 mesh-shader probe (dxc-compiled `ms_6_5`: meshlet
with a position+color payload, `SetMeshOutputsEXT`-equivalent) → DispatchMesh →
render target readback pixel-exact. Also: the mesh output count, the per-primitive
exports, the indirect dispatch.

## Slice 2 — M10: VK_KHR_fragment_shading_rate (VRS commands) — CLOSED (documented limitation)
The tier 2 is reported; the Metal rate-map path exists (MTLRasterizationRateMap,
the MTL4 render pass's `rasterizationRateMap`).
2.1 Extension + features (`pipelineFragmentShadingRate`, `attachmentFragmentShadingRate`,
`primitiveFragmentShadingRate`) + properties (`minFragmentShadingRateAttachmentTexelSize`,
`maxFragmentShadingRateAttachmentTexelSize`, `fragmentShadingRateNonTrivialCombinerOps`).
2.2 **Rate-map creation** — the pipeline's `VkPipelineFragmentShadingRateStateCreateInfoKHR`
→ `MTLRasterizationRateMapDescriptor` (layers/rates) + `newRasterizationRateMapWithDescriptor`.
2.3 **Attachment path** — `vkCmdSetFragmentShadingRateKHR` with the shading-rate
image → the MTL4 render pass's `rasterizationRateMap`; the render-pass plumbing
for the rate image attachment.
2.4 **Per-primitive VRS** — the decision gate from the roadmap's 4C.3: the Metal
has no shader-side per-primitive rate; evaluate (a) rate-map emulation via a
primitive-classification pass, (b) vkd3d-side expansion via the mesh shaders
once Slice 1 lands, (c) document as the hard blocker (the tier 2 stays the
reported gate; the per-primitive API fails cleanly). Document the choice with
the probe evidence.
2.5 **Acceptance**: a D3D12 VRS probe (dxc shaders + `SetShadingRate` + the
shading-rate image) — render with a 2x2 rate over a region → the readback's
effective resolution halved (count the rasterized pixels); the combiner ops
(min/max/sum) verified.

## Slice 3 — M10: sampler-feedback shader path — CLOSED (documented limitation)
The tier 0.9 is reported; the Metal has no 64-bit image atomics.
3.1 Inspect the vkd3d's sampler-feedback DXIL (the 64-bit atomic ops on the
feedback resource) + the SPIR-V.
3.2 **Emulation** — the MVK's shader lowering for the 64-bit image atomics on
the feedback resource: the roadmap's 32-bit atomic encoding (a 2-word CAS
scheme or a per-feedback-region atomic counter) — the exact scheme chosen by
the SPIR-V analysis; the feedback resource's layout must stay compatible with
the vkd3d's expectations.
3.3 **Acceptance**: a D3D12 sampler-feedback probe (dxc `SampleCmpLevelZero`
+ `WriteSamplerFeedback`) — the feedback resource's mip/written counts match
the expected values on the GPU readback.

## Slice 4 — CR tier 3: the InnerCoverage input — DONE (2026-08-15, D3D12-path probe green)
The tier 3 is reported (the fork relaxation); the MVK CR emulation provides the
post-snap overestimation.
4.1 SPIRV-Cross: `BuiltInFullyCoveredEXT` → the MSL emission (the
`[[inner_coverage]]`-style input or a computed value).
4.2 MVK CR fragment emulation: the fully-covered computation (the 4 sample
corners inside the original triangle — reuse the CR's bisector varyings).
4.3 **Acceptance**: a D3D12 InnerCoverage probe — a fragment shader reading the
`InnerCoverage` input, rendered with the CR mode → the fully-covered pixels'
count matches the reference (computed by the center+corner test in a control
probe); the pixel-exact CR probes stay green (regression).

## Slice 5 — CORE_1_0 remainder (roadmap 5.3) — DONE (corpus 8/8, 83da2d8)
5.1 **SM 6.0 compute corpus** (dxc): the wave ops, the 64-bit int ops, the
derivatives (in compute), the structured-buffer UAVs — each compiled to the
DXIL, dispatched on the CORE_1_0 device, readback verified.
5.2 **No-graphics negative**: the compute-only device rejects the graphics-queue
requests (the vkd3d's CreateCommandQueue with the GRAPHICS type → the failure);
the graphics-PSO creation on the CORE device → the failure.
5.3 **Acceptance**: the corpus matrix (the N shaders × the readback) + the two
negative tests, all on `min 1_0_CORE` devices.

## Slice 6 — M14 ship (public VKD3D-Proton-MacOS repo + implementation tarball)

STATUS 2026-08-15: the implementation tarball is BUILT and verified
(`scripts/ship-m14.sh` -> `artifacts/VKD3D-Proton-MacOS-2026-08-15.tar.zst`,
sha256 8fbb6924..., 15922 entries; the pinned lane hashes inside it match the
pins exactly — `artifacts/evidence/2026-08-15-m14-ship-manifest.md`). The
remaining step is the user's GitHub action: create the public repo, push all
commits, attach the artifact to a release, verify-by-redownload.
6.1 **Public repo**: publish the VKD3D-Proton-MacOS repo on GitHub (public),
push ALL commits (the workspace + the four fork sub-repos' commits are
recorded in the evidence; the fork trees + their git histories are included in
the artifact below so the result is rebuildable from scratch).
6.2 **Implementation tarball**: `scripts/ship-m14.sh` assembles
`artifacts/VKD3D-Proton-MacOS-<date>.tar.zst` containing the FULLY WORKING
implementation:
  - the workspace's committed state (README, ROADMAP, docs/, scripts/,
    artifacts/evidence/ — all runtime-verified evidence)
  - the four fork source trees with their git histories (MoltenVK,
    vkd3d-proton, SPIRV-Cross, MetalSharp) — build/ dirs excluded, rebuildable
  - the built artifacts: the d3d12.dll/d3d12core.dll pair (x86_64) + the
    libMoltenVK.dylib + MoltenVK_icd.json
  - the repackaged `metalsharp-graphics-dll-m12.tar.zst` bundle
  - a BUILD.md describing the exact toolchain + build steps + the launch env
6.3 **Verify-by-redownload**: download the published artifact from the public
repo's release, re-extract, re-hash — the d3d12 pair + libMoltenVK + the
bundle's lanes must match the pinned hashes exactly.
6.4 **Runtime validation**: the artifact's lanes under the wine (the stage-dxr
launch env, but loading the ARTIFACT's copies): the flprobe ladder + the
compute matrix + the DXR probe against the artifact's d3d12 pair + libMoltenVK
(not the stage copies).
6.5 **Regression games**: launch the D3D12 games through the artifact's bundle;
the smoke tests green (the DXR/RayQuery titles, the mesh titles once Slice 1
lands, the CR titles).

---

## Ordering rationale
Slice 1 (mesh) is the largest and unlocks the per-primitive VRS option (2.4b).
Slices 2–4 are independent of each other and can run in parallel once the dxc
tooling lands. Slice 5 is small and independent. Slice 6 waits on 1–5 for the
final bundle refresh (the hash pins change again).

## Risk register
- **Per-primitive VRS** (2.4): the Metal has no native shader-side rate — the
  emulation may be impossible exactly; the documented fallback keeps the tier-2
  gate (the per-primitive API fails cleanly).
- **Sampler-feedback emulation** (3.2): the 32-bit encoding must match the
  vkd3d's expected feedback layout — the SPIR-V analysis is the gate.
- **Mesh payload sizes**: the Metal's mesh payload limits vs the D3D12's
  expected payloads — the probe defines the practical ceiling.
- **dxc availability**: the tooling prerequisite gates the probe slices.
