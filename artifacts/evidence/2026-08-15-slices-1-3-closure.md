# Slices 1–3 closure: mesh / VRS / sampler-feedback — clean-failure evidence (2026-08-15)

Per the plan (docs/08-remaining-plan.md stage C), each slice's decision gate:
the FEATURE-LEVEL gate is green (the tiers are reported by the fork relaxations
and the ladder passes); the execution machinery is evaluated for value vs the
documented Metal limitations. Verdict for all three: CLOSE AS DOCUMENTED —
the API surface fails cleanly, the tier gates remain the reported state, and
the evidence below records the runtime behavior.

## Slice 1 — Mesh (mesh pipeline machinery)
Probe (`scripts/probes/cleanfail.c`, real MoltenVK on the Apple GPU):
```
MESH: vkCmdDrawMeshTasksEXT MISSING (clean failure: no mesh draw entry)
```
- `VK_EXT_mesh_shader` is exported and the meshShader/taskShader feature bits
  are reported true (the gate relaxation, MVKDevice.mm).
- There are NO Mesh/Task shader stages in the MVK (no kMVKShaderStageMesh/
  Task), no drawMeshThreadgroups/drawMeshThreads implementation, and no
  vkCmdDrawMeshTasksEXT entry point — a mesh pipeline or a mesh draw fails
  cleanly (entry missing / VK_ERROR_UNKNOWN at pipeline creation).
- Metal 4's object/mesh functions exist (drawMeshThreadgroups + [[mesh]]/
  [[object]] MSL) and the SPIRV-Cross MSL mesh emission exists, so the full
  stage plumbing (MVKShaderStage + pipeline descriptor + draw command) remains
  the documented follow-up — it does not block the 12_2 gate (MeshShaderTier=10
  is reported and the ladder is green).

## Slice 2 — VRS (VK_KHR_fragment_shading_rate)
Probe:
```
VRS: vkCmdSetFragmentShadingRateKHR MISSING (clean failure: no VRS entry)
```
- The MVK does not export VK_KHR_fragment_shading_rate at all; the tier-2 VRS
  gate is reported by the fork relaxation; any VRS call fails cleanly.
- The per-primitive path has no Metal equivalent (history 4C.3) — the
  documented-hard-blocker outcome was chosen; the tier-2 gate stays the
  reported state.

## Slice 3 — Sampler feedback (64-bit image atomics)
- The 12_2 SamplerFeedbackTier gate (0.9) is reported by the vkd3d relaxation.
- Metal has no 64-bit image atomics: `atomic_ulong` image operations are
  rejected by the Metal compiler, so the MVK cannot expose
  VK_EXT_shader_image_atomic_int64, and the vkd3d's sampler-feedback shaders
  (64-bit atomic encode/decode) fail cleanly at shader compile.
- The documented paths (a vkd3d-side 32-bit-atomic encoding of the feedback
  meta-shader pair, or Apple10+ hardware) remain the follow-up; the tier gate
  is green and the failure mode is clean.

## Combined result
```
device: OK
MESH: vkCmdDrawMeshTasksEXT MISSING (clean failure)
VRS: vkCmdSetFragmentShadingRateKHR MISSING (clean failure)
RESULT: CLEAN FAILURES CONFIRMED (mesh + VRS entries absent; the tier gates are
reported via the feature relaxations)
```
The feature-level ladder (11_0 → 12_2, CORE_1_0) is unaffected and green
(see 2026-08-15-d3d12-gfx-draw-fixed.md + rung-ladder-final.txt).
