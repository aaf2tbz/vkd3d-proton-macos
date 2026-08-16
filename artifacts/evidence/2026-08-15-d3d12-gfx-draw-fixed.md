# D3D12 graphics-draw path FIXED + Slice 4 (CR tier-3 InnerCoverage) verified (2026-08-15)

## Root cause of the D3D12 draw-nothing bug (cross-cutting blocker, now FIXED)
The vkd3d makes the vertex stride DYNAMIC (`VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE`),
so the pipeline's static vertex-binding stride is **0**. The MVK's conservative-
rasterization emulation fetched the triangle's positions with the pipeline's
static stride (`_mvkCRPosFetch`: `idx * stride`), so every vertex fetched the
SAME position → a degenerate triangle → zero fragments. Every D3D12 graphics
draw (CR on or off) produced nothing; the clear + the copy worked, so the
render path itself was fine.

Fix (`MVKCommandBuffer.mm`, `MVKCommandEncoder::setCRConstants`): when the
pipeline's vertex stride is dynamic, the position-fetch stride now comes from
the ENCODER's bound vertex-buffer state (`_vertexBuffers[pbIdx].stride`, set by
`vkCmdBindVertexBuffers2`), not the static pipeline value.

Diagnostic trail (all recorded):
- The vkd3d emits the full command stream (vkCmdBindPipeline,
  vkCmdSetViewportWithCount, vkCmdSetScissorWithCount, vkCmdSetPrimitiveTopology,
  vkCmdBindVertexBuffers2, vkCmdBeginRendering, vkCmdDraw, vkCmdEndRendering).
- The MVK render pass + the draw state were all correct (attachment fmt=70,
  LOAD/STORE, non-nil MTLRenderPipelineState, triangle, 3 verts, viewport
  (0,64,64,-64)), the Metal API+GPU validation was silent.
- A native-Vulkan replicate of the vkd3d's exact pipeline structure (its own
  dumped SPIR-V, the CR overestimate, the EXT dynamic states, the negative
  viewport, the dynamic rendering) drew correctly — proving the MVK path.
- The pipeline dump exposed the static stride = 0 (dynamic stride case) —
  the smoking gun.

## Slice 4 acceptance: CR TIER 3 INNERCOVERAGE WORKS (D3D12 PATH)
`cr_inner_probe` (D3D12, wine, real MoltenVK on the Apple M4):
```
CR tier: 3 (hr=0x00000000)
graphics PSO (CR on + InnerCoverage PS): 0x00000000
fragments=1691 fc=1691 inconsistent=0
RESULT: CR TIER 3 INNERCOVERAGE WORKS (D3D12 PATH)
```
The PS reads SV_InnerCoverage; the emulation computes the fully-covered test
(all 4 pixel corners inside the ORIGINAL pre-snap triangle) in the injected
MSL. For every pixel where the fragment ran (1691 — matching the native
Vulkan probe's 1691), the emulated fc bit equals the corner test computed from
the measured fragment position + the emulation's varyings: **0 inconsistencies**.
This is the first D3D12-graphics-render probe in the session and it is green;
all earlier D3D12 probes were compute-only.

## Regression status (real MoltenVK, staged binaries)
flprobe ladder — all green:
```
min 12_2 / 12_1 / 12_0 / 11_1 / 11_0 / 1_0_CORE : dev=CREATED
FEATURE_LEVELS max=12_2; TiledResourcesTier=4; ConservativeRasterTier=3
ROVsSupported=1; OutputMergerLogicOp=1; DepthBoundsTestSupported=1
OPTIONS5 (mingw id 27): RaytracingTier=11
```
The feature-level gates (11_0 → 12_2, CORE_1_0) remain green with the fix in
place.
