# Remaining Items — Execution Plan (2026-08-15)

The feature-level ladder (11_0 → 12_2, CORE_1_0) is GREEN on real MoltenVK, and
every Vulkan-level execution probe passes (CR pixel-exact, logic-op 16/16,
64-UAV, sparse, depth-bounds, ray-query, DXR 1.1, CORE_1_0 corpus 8/8). The
remaining issues all cluster around ONE cross-cutting blocker plus three
documented-limitation slices plus shipping. This plan closes each item with a
concrete path, a decision gate, and a fallback so nothing is left dangling.

---

## Phase A — Fix the D3D12 graphics-draw path (the cross-cutting blocker)

**Symptom (reproduced):** `red_off_probe` / `cr_inner_probe` — the graphics PSO
creates, `ClearRenderTargetView` lands (the vkd3d clears via a compute shader,
so that proves the queue + the barrier + the copy all work), but the DRAW
produces zero fragments, even with CR OFF and a trivial always-red PS. The
readback is 100% the clear color.

**What is already ruled out:**
- The negative viewport height (the vkd3d emits `(0, H, W, -H)`): a native
  Vulkan probe with that exact viewport still draws (red=1691 landed).
- The CR injection: the CR-off probe fails identically.
- The PSO/pipeline creation: it returns S_OK and the MSL is correct.

**Remaining suspects, in order:**

1. **The vkd3d never emits the draw / the render-pass commands.** The vkd3d's
   deferred command list may be dropping the draw (e.g. an invalid pipeline
   state at draw time, a `d3d12_command_list` state check, or the graphics
   root signature / topology state not recorded).
2. **The render pass is never begun.** The vkd3d uses dynamic rendering
   (`vkCmdBeginRendering`). If the begin/end is skipped or the view mask /
   attachments are wrong, the MVK may never open an `MTLRenderCommandEncoder`,
   and `drawPrimitives` would be silently ignored or logged as an error.
3. **The vertex-buffer/vertex-fetch path.** The stage-in VS
   (`float2 POSITION [[attribute(0)]]`) may be reading a wrong buffer (the
   vkd3d's UPLOAD-heap buffer binding vs the MVK's expected index).

**Diagnostic steps (each is one command, in order):**

- A1. Run with `VKD3D_DEBUG=trace` (the vkd3d env var) and grep for
  `vkCmdBeginRendering`, `vkCmdBindPipeline`, `vkCmdBindVertexBuffers`,
  `vkCmdDraw` in the trace. This splits the problem in half: commands missing
  on the vkd3d side vs commands ignored on the MVK side.
- A2. Write `scripts/probes/gfx/min_tri.c`: the smallest possible D3D12
  graphics probe — a procedural VS (fullscreen triangle from `SV_VertexID`,
  **no vertex buffer at all**), always-red PS, no CR, no inner coverage,
  draw + readback. If this draws → the VB path is the bug. If it doesn't →
  the draw/render-pass path is the bug.
- A3. On the MVK side, temporarily add `MVKLogInfo` in
  `MVKCommandEncoder::beginRendering` / `drawPrimitives` (or run with
  `MVK_CONFIG_LOG_LEVEL=3`) to confirm the encoder is entered.
- A4. Fix at whichever layer A1–A3 indicts. The most likely fix spots, based
  on what is known to work (the compute path, the native-Vulkan probes):
  - vkd3d: `d3d12_command_list` draw recording / dynamic rendering emission
    (`command.c`, `d3d12_command_list_DrawInstanced` →
    `d3d12_command_list_execute`).
  - MVK: the dynamic-rendering begin (the `MVKCommandEncoder::beginRendering`
    with the negative-height viewport + the `VkRenderingInfo` from the vkd3d).

**Decision gate:** Phase A is done when `min_tri.c` (and then `cr_inner_probe`)
land pixel-verified output. Budget: this is THE blocker — spend what it takes,
but check in every 2 hours of wall-clock; if A1–A4 have all been tried and the
draw still doesn't land, capture the traces in
`artifacts/evidence/2026-08-15-d3d12-gfx-draw-blocker.md` and move to the
fallback (Phase A′ below) rather than stalling the rest of the plan.

**Fallback A′ (documented limitation):** the D3D12 compute path + the
Vulkan-level graphics probes are the recorded evidence for the feature levels;
the D3D12-graphics-render path is documented as the known blocker with the
exact repro, the traces, and the analysis. The roadmap's gate stays honest
(green ladder + Vulkan-level execution proofs + the documented gap).

---

## Phase B — InnerCoverage verification (depends on A)

**Current state:** the emulation is implemented and committed (MVK `dfd3c70` +
the SPIRV-Cross patch). The native-Vulkan probe proves the machinery is
self-consistent: the observed fully-covered output is EXACTLY what the injected
MSL computes from the observed fragment positions (0 inconsistencies over 4096
pixels). Two open items:

- B1. After Phase A, run `cr_inner_probe` (the D3D12-level probe) and compare
  against the CPU reference.
- B2. The native probe's reference mismatch (the observed fc region vs the
  4-corner model) traces to the fragment `[[position]]` being offset by ~1 px
  in x at the sampled row. With the position map in hand, recompute the
  reference FROM THE OBSERVED positions; if the fc matches the position-based
  reference exactly (it already does — 0 inconsistencies), the remaining task
  is to explain/fix the 1-px position offset (suspect: the readback row order
  or the pixel-center convention in the probe's reference), then re-verify.

**Decision gate:** either the D3D12 probe passes against the corrected
reference (slice closed green), or the evidence documents the self-consistency
proof + the open 1-px offset with the data attached (slice closed honestly).

---

## Phase C — Slice 1 (mesh), Slice 2 (VRS), Slice 3 (sampler feedback)

**Reality check (already established):**
- Mesh: `VK_EXT_mesh_shader` is advertised (meshShader/taskShader=1 in the
  mvkprobe) but `MVKShaderStage` has no Mesh/Task stages — a real pipeline
  creation with a mesh stage fails. The Metal 4 object/mesh API exists; the
  full stage plumbing (stages, pipeline descriptor, `vkCmdDrawMeshTasksEXT`)
  is a large slice.
- VRS: Metal has no shader-side per-primitive shading rate; `VK_KHR_fragment_
  shading_rate` is NOT exported by the MVK at all (confirmed in the mvkprobe).
- Sampler feedback: Metal has no 64-bit image atomics; the feedback shaders
  need a 2-word atomic emulation with a vkd3d-compatible layout.

**Plan per slice — decision gate BEFORE implementation:** the feature-level
gate itself is already green (MeshShaderTier=10, the VRS tier, the
SamplerFeedbackTier=90 are reported by the fork relaxations and the ladder
passes). The question for each slice is whether execution machinery adds
proven value within the remaining budget:

- C1. **Mesh (Slice 1)** — worth attempting ONLY if Phase A lands quickly,
  because the mesh probe needs the working D3D12 draw path. Time-box: 4 hours.
  First milestone: `MVKShaderStage` + a native-Vulkan `vkCmdDrawMeshTasksEXT`
  probe (no D3D12 involved). If the milestone is not reached in the time-box,
  close the slice as documented: the gate green, the mesh pipeline creation
  fails cleanly (verify + capture the exact error).
- C2. **VRS (Slice 2)** — the per-primitive path is impossible exactly (no
  Metal equivalent); the attachment rate-map path needs the VK extension
  plumbing the MVK doesn't export. CLOSE AS DOCUMENTED: capture the probe that
  proves the API fails cleanly (the device doesn't advertise
  `VK_KHR_fragment_shading_rate` → the vkd3d's VRS calls fail predictably).
  Document the roadmap 4C.3 decision as: the tier-2 gate reported; the
  per-primitive API unsupported, fails cleanly.
- C3. **Sampler feedback (Slice 3)** — CLOSE AS DOCUMENTED with the same
  pattern: the Metal lacks 64-bit image atomics (recorded), the tier 0.9 gate
  reported, the `WriteSamplerFeedback` shader fails cleanly (verify + capture).

Each closed slice gets an evidence file in `artifacts/evidence/` with the
runtime proof of the clean failure + the analysis, and the roadmap status lines
updated from `[ ]` to the closed state.

---

## Phase D — Ship (Slice 6)

Already done: `scripts/ship-m14.sh` + the verified tarball + the manifest.
Remaining (all executable today, `gh` authed as `aaf2tbz`):

- D1. Rebuild the final artifacts (the d3d12 pair + libMoltenVK with all the
  committed fixes), refresh the hash pins, re-run `scripts/ship-m14.sh`,
  re-verify the manifest.
- D2. `gh repo create` (public) → push all commits → attach the tarball to a
  release.
- D3. Verify-by-redownload: fetch the release asset, re-hash against the pins.
- D4. Runtime-validate: run the flprobe ladder + the corpus + the DXR probe
  against the ARTIFACT's copies (not the stage-dxr ones).
- D5. Final docs sweep: `ROADMAP.md`, `docs/07-followup-roadmap.md` statuses,
  the evidence index; commit everything.

---

## Final gate (goal completion)

The goal closes when: the ladder evidence is green on the final staged
binaries, every slice in `docs/07-followup-roadmap.md` is either DONE with a
runtime acceptance gate or CLOSED with the documented-limitation evidence, the
repo is public with the release attached and re-verified, and the final commit
records it all. No item may remain "blocking" — each is completed or closed
one way or another, exactly as the roadmap requires.
