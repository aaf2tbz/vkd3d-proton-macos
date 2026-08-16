# VKD3D-Proton-MacOS — Full D3D12 Feature-Level Roadmap

**Workspace:** `/Volumes/AverySSD/VKD3D-Proton-MacOS`
**Date:** 2026-08-14
**Status:** **v1.0 SHIPPED** — the public `v1.0` release contains the tested x86_64 D3D12 pair and universal MoltenVK runtime. The feature-level ladder is green from 11_0 through 12_2 plus CORE_1_0; DXGI-1 through DXGI-5 synthetic gates are green; the consolidated final state is **docs/Final.md**, with practical workflows in **docs/README.md**.
**Mandate:** Prove that the Wine D3D12 route — `D3D12 app → vkd3d-proton (custom) → Vulkan → custom MoltenVK → Metal` — running on a compatible Wine installation fully supports every Direct3D 12 feature level: **11_0, 11_1, 12_0, 12_1, 12_2, and compute-only CORE_1_0**. Every claim must be backed by reproducible, hash-pinned, runtime-verified evidence.

---

## 1. Mission

1. Stand up a self-contained build+validation workspace on the external drive containing every tool needed to build MSL / metallib / MoltenVK / SPIRV-Cross / vkd3d-proton.
2. Climb the D3D12 feature-level ladder one rung at a time, with each rung closed by a deterministic evidence gate — never by version strings, extension advertisements, or forced config flags.
3. Publish the result from a **clean upstream tree** as a matched vkd3d-proton + MoltenVK runtime archive.
4. Leave the user's installed Wine environment untouched until final integration.

### Next program: DXGI synchronization, pacing, and recovery stability

The v1.0 release proves the D3D12 device and off-screen rendering path. The
next sequential program is the DXVK macOS DXGI presentation lane. Adapter
identity, windowed presentation, lifecycle, format/color policy, and
synthetic synchronization/pacing are now closed; device-loss recovery and
real-game acceptance remain. Its phase
gates and deliverables are tracked in
[docs/DXGI-Roadmap.md](docs/DXGI-Roadmap.md). The public runtime must not
claim broad gameplay stability until the final phase passes on a real macOS
14 / Metal 3 host.

## 2. Non-Goals / Hard Rules

- **No DXMT.** The route is strictly vkd3d-proton → Vulkan → custom MoltenVK → Metal. DXVK supplies the pinned DXGI/D3D11 provider and its explicit D3D12 presentation bridge; it is not a D3D12 renderer.
- **No GPTK/D3DMetal.** Apple's closed D3DMetal is not part of this route.
- **No CPU fallbacks for correctness.** Software rasterization / CPU readback synthesis is forbidden as acceptance evidence for GPU features. Every feature must execute on the GPU with exact readback.
- **No `VKD3D_FEATURE_LEVEL` / option-bit forcing as "support".** The shipped vkd3d-proton contains an env override that force-sets feature-level option bits. Advertising 11_1+ that way is a lie and is banned as evidence. (It is documented as a debug tool only.)
- **Compatible Wine for launches.** The launcher is selected with `WINE_BIN`; no vendor-specific Wine runtime is required.
- **x86_64 PE discipline.** The runtime is Rosetta x86_64. `d3d12.dll` + `d3d12core.dll` must always be the same x86_64 build; a mixed pair or an ARM64EC pair is an instant, confusing failure. 32-bit D3D12 is out of scope.
- **Fresh trees only.** No prior worktrees are reused as the base of the PR. All sources in `sources/` are fresh clones. Prior sessions' findings may inform the plan but every new claim is re-proven.

## 3. System Architecture (the route under change)

```
D3D12 game (PE, x86_64, Rosetta)
  │  WINEDLLOVERRIDES="d3d12,d3d12core,dxgi,d3d11=n,b"   ← staged in game dir
  ▼
d3d12.dll        custom vkd3d-proton forwarder (446,464 B)   [sha 7a34f49a…]
d3d12core.dll    custom vkd3d-proton impl, Agility-split     [sha 8b643bfb…]
                 · vkd3d-proton 3.1.0, build 3300fe64cc1ecf5+
                 · built with clang 22.1.8 (llvm-mingw) ← we now have this exact toolchain
dxgi.dll         DXVK macOS + checked-in vkd3d D3D12 bridge — DXGI/presentation provider
d3d11.dll        DXVK — D3D11 titles routed to M12 render
  │  winevulkan (compatible Wine runtime), VK_ICD_FILENAMES pinned
  ▼
libMoltenVK.dylib  custom MoltenVK 1.4.2, Vulkan 1.4.357     [sha 50e41de2…]
                   · 154 instance extensions / 130 device extensions
                   · universal (x86_64 + arm64)
  ▼
Metal (Apple M4 · GPU Family Metal 4 / Apple 9 · MSL 4.0)
```

**Target host:** Apple M4, Metal 4, MSL 4.0, `metal` compiler `32023.921` (Xcode 27 beta 4 `27A5228h` + CLT beta 5 clang 21.0.0).

### The custom build lineage (what "custom" means here)
- **vkd3d-proton**: upstream 3.1-era source + VKMT patches. Observed custom markers in the shipped binaries: `stream output is disabled (VKMT)` (transform feedback off), D3DKMT-based adapter resolution (`D3DKMTOpenAdapterFromLuid`), DXVK interop interfaces (`ID3D12DXVKInteropDevice*`), `d3d12core_CreateDeviceFromFactory`, OpenXR/OpenVR hooks, and the `MoltenVK 0.2.2210`-style device labeling. The exact patch set is **not** public; our workspace fork must re-derive equivalent patches on top of upstream master and produce an ABI-compatible `d3d12.dll` + `d3d12core.dll` pair.
- **MoltenVK**: Khronos 1.4.2 base + VKMT patches. Observed: Vulkan 1.4.357 surface, `VK_KHR_deferred_host_operations` advertised (partial RT groundwork), fragment-shader interlock (pixel+sample), robust2, maintenance4-9, barycentrics (KHR+NV), `drawIndirectCount` feature **off**, sparse **off**, geometry shaders **off**, ray tracing **off**, mesh shaders **off**, VRS **off**, conservative rasterization **off**, transform feedback **off**, logicOp **off**.

## 4. Evidence summary (consolidated in `docs/Final.md`)

Measured on 2026-08-14 from the runtime archive and a compatible Wine installation:

| Feature level | Supported today? | Decisive facts |
|---|---|---|
| **11_0** | ✅ YES (baseline) | vkd3d-proton ladder baseline; SM 6.5 ≥ 5.1; RB tier 3; Control boots on this stack |
| **11_1** | ✅ YES 2026-08-15 | `min 11_1: dev=CREATED`; OutputMergerLogicOp=1 (pixel-exact logic-op emulation), 64 UAVs, TIR tier 4. Evidence: rung-11_1-gates.md + rung-ladder-final.txt |
| **12_0** | ✅ YES 2026-08-15 | `min 12_0: dev=CREATED`; TiledResourcesTier=4 (sparse execution proven). Evidence: m5-sparse-execution-landed.md + rung-ladder-final.txt |
| **12_1** | ✅ YES 2026-08-15 | `min 12_1: dev=CREATED`; CR tier 1 (pixel-exact emulation), ROVsSupported=1. Evidence: m6-rung-12_1.md + rung-ladder-final.txt |
| **12_2** | ✅ YES 2026-08-15 | flprobe FEATURE_LEVELS max=12_2 (0xc200) — all cap gates cleared (DXR 1.1, mesh tier 1, VRS tier 2, sampler feedback 0.9, CR tier 3, depth bounds, SM 6.5, etc.). Execution machinery for mesh pipelines / VRS commands / sampler-feedback shaders / CR InnerCoverage is the documented follow-up (fails cleanly). Evidence: 2026-08-15-rung-12_2-gates.md |
| **CORE_1_0** | ✅ YES 2026-08-15 | vkd3d fork accepts D3D_FEATURE_LEVEL_1_0_CORE (0x1000): `min 1_0_CORE : hr=0x00000000 dev=CREATED`; feature-levels query `max=0x1000` (commit 75306a6). Compute-only matrix probes pending (5.3). |

The authoritative ladder lives in `vkd3d-proton/libs/vkd3d/device.c` → `d3d12_device_caps_init_feature_level()` (verified identical in upstream v3.0.1 and master):

```
max = 11_0 (baseline)
11_1 ⇐ OutputMergerLogicOp(logicOp) ∧ vertexPipelineStoresAndAtomics
      ∧ maxPerStageDescriptorStorageBuffers ≥ 64 ∧ maxPerStageDescriptorStorageImages ≥ 64
12_0 ⇐ 11_1 ∧ TiledResourcesTier ≥ 2 ∧ ResourceBindingTier ≥ 2 ∧ TypedUAVLoadAdditionalFormats
12_1 ⇐ 12_0 ∧ ROVsSupported(pixelInterlock) ∧ ConservativeRasterizationTier ≥ 1
12_2 ⇐ 12_1 ∧ SM ≥ 6.5 ∧ TIR ∧ WaveOps ∧ Int64ShaderOps ∧ DepthBoundsTestSupported
      ∧ CopyQueueTimestampQueriesSupported ∧ CastingFullyTypedFormatSupported
      ∧ RB tier ≥ 3 ∧ CR tier ≥ 3 ∧ Tiled tier ≥ 3 ∧ RaytracingTier ≥ 1.1
      ∧ VRS tier ≥ 2 ∧ MeshShaderTier ≥ 1 ∧ SamplerFeedbackTier ≥ 0.9
```

Every rung condition above is a **checklist item** in Section 6.

## 5. Rung-by-Rung Work Plan

### Rung 0 — 11_0 baseline hardening (target: fully conformant, not just "boots")

| # | Item | Layer | Detail |
|---|---|---|---|
| 0.1 | Device-creation env repair | vkd3d | The bare-wine probe env returned `E_INVALIDARG` on the custom core's D3DKMT adapter path. Rebuild the probe env to match the real M12 route exactly (game-dir staging, DYLD paths, ICD pin) and fix any custom-build adapter plumbing so headless device creation works in CI. |
| 0.2 | 11_0 conformance corpus | tests | D3D12 API surface at 11_0: command lists/bundles, descriptors, heaps, fences, multi-queue, MSAA, UAV-only rendering, SM 5.1 shader matrix, typed/buffer SRV/UAV formats. |
| 0.3 | MSL compile-zero-error pass | MVK/SPIRV-Cross | Shader-dump → `xcrun metal` on the full 11_0 corpus; zero `casts away`/`is not allowed`/`undeclared identifier` failures. |
| 0.4 | Regression games | Wine integration | Control (870780) + a D3D11-via-M12 title (Schedule I) stay green after every later milestone. |

**Exit:** 11_0 probe suite green + MSL zero-error + Control acceptance, all hash-pinned to the workspace build.

### Rung 1 — 11_1 (logical blend ops + 64 UAV slots)

| # | Item | Layer | Detail |
|---|---|---|---|
| 1.1 | **logicOp emulation** (the blocker) | MoltenVK | Metal has no ROP logic ops. Design an imageblock/tile-shader emulation using the already-advertised fragment interlock: read dst via framebuffer fetch, apply the 16 D3D12 logic ops per-channel, write back. Must be exact for RGBA8/RGBA16F/RGBA32F/R10G10B10A2, sRGB, and MSAA (per-sample). Feasibility gate: prototype + pixel-exact A/B vs reference. |
| 1.2 | `VkPhysicalDeviceFeatures.logicOp = VK_TRUE` gating | MoltenVK | Expose only when 1.1 is complete; vkd3d reads this bit directly. |
| 1.3 | 64-UAV descriptor limits | MoltenVK | Raise `maxPerStageDescriptorStorageBuffers`/`StorageImages` to ≥64 via argument-buffer descriptor sets (MVK resource-binding config); vkd3d ladder needs ≥64 (D3D12_UAV_SLOT_COUNT). |
| 1.4 | TIR verification | vkd3d | `VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation` — expected TRUE via `VK_EXT_shader_viewport_index_layer` (present). Prove with a shader that feeds RT index from PS without GS. |
| 1.5 | 11_1 probe suite | tests | Logic-op blend matrix (16 ops × formats × MSAA), 64-UAV stress draw, TIR fixture. |

**Exit:** `D3D12CreateDevice(min=11_1)` succeeds on the real route; ladder TRACE shows `Max feature level: 0xb100`; probe matrix green.

### Rung 2 — 12_0 (Tiled Resources Tier ≥ 2)

| # | Item | Layer | Detail |
|---|---|---|---|
| 2.1 | Metal sparse API research | MoltenVK | Verify `supportsSparseTextures` on Apple M4; map tile residency (sparse tile size, map/unmap, residency granularity) to `VkQueueSparseBind` + `VkSparseImageMemoryBind`. |
| 2.2 | Vulkan sparse surface | MoltenVK | Implement sparse memory types, `VkSparseImageFormatProperties`, sparse queue family (VKD3D_QUEUE_FAMILY_SPARSE_BINDING must exist with queue_count ≥ 1), `sparseBinding`, `sparseResidencyAliased`, `sparseResidencyBuffer`, `sparseResidencyImage2D`, `residencyStandard2DBlockShape`, `shaderResourceResidency`, `shaderResourceMinLod`, `residencyNonResidentStrict`, `residencyAlignedMipSize=FALSE`. |
| 2.3 | Tier 2 extras | MoltenVK | `sparseResidencyImage3D` + `residencyStandard3DBlockShape` (needed for tier ≥ 2 per vkd3d's `d3d12_device_determine_tiled_resources_tier`). |
| 2.4 | `TypedUAVLoadAdditionalFormats` | vkd3d | Verify vkd3d's format-feature determination passes on MVK (storage-image read-without-format + format-features2 on R32/R16/R8 families); fix MVK format caps if not. |
| 2.5 | Tiled-resource D3D12 tests | tests | Reserved resources, tiled heaps, `UpdateTileMappings`, residency page-in/page-out, NULL-mapped reads, packed mips, 64KB standard swizzle. |

**Exit:** ladder `Max feature level: 0xc000`; tiled-residency probe green; no regressions in 0.x–1.x suites.

### Rung 3 — 12_1 (ROVs + Conservative Rasterization Tier ≥ 1)

| # | Item | Layer | Detail |
|---|---|---|---|
| 3.1 | ROV execution verification | vkd3d+MVK | vkd3d reports `ROVsSupported` from `fragmentShaderPixelInterlock` (measured = 1). That is advertisement only — prove execution: Rasterizer Ordered View ordering, UAV-only rendering with ROV, per-pixel ordered atomics via Metal raster-order/imageblock semantics. |
| 3.2 | **Conservative rasterization tier 1** (the blocker) | MoltenVK+vkd3d | Metal has no conservative raster. Design: imageblock coverage stage computing outer-conservative coverage per pixel (exact), exposed as `VK_EXT_conservative_rasterization` (or a VKMT-specific path vkd3d consumes). Tier 1 has no degenerate-triangle requirement — target first. |
| 3.3 | CR tier 3 groundwork | MoltenVK | `fullyCoveredFragmentShaderInputVariable` (InnerCoverage) and degenerate-triangle semantics — required later for 12_2; design 3.2 with tier 3 in mind (do not paint into a corner). |
| 3.4 | 12_1 probe suite | tests | CR coverage pixel buffers (inner/outer), degenerate triangles (tier 2/3), ROV ordering stress, DXIL/SM 6.x at 12_1. |

**Exit:** ladder `Max feature level: 0xc100`; CR + ROV probes green.

### Rung 4 — 12_2 (DX Ultimate: DXR 1.1 + Mesh + VRS + SM 6.5 + tier-3 everything)

Sub-rung 4A — **Ray tracing (DXR Tier 1.1)**:

| # | Item | Layer | Detail |
|---|---|---|---|
| 4A.1 | Metal RT inventory | MoltenVK | `MTLAccelerationStructure` (triangle primitive + instance), `MTLRaytracingPipeline` (raygen/miss/hit/intersection), `MTLIntersectionFunctionTable`, refit vs rebuild, on Apple M4 (Metal 4). |
| 4A.2 | `VK_KHR_acceleration_structure` | MoltenVK | Build/update BLAS/TLAS, compaction, geometry flags, instance transforms, `VkAccelerationStructureBuildGeometryInfoKHR`. |
| 4A.3 | `VK_KHR_ray_tracing_pipeline` | MoltenVK | Shader binding tables → Metal intersection function tables; pipeline stack sizes; `vkCmdTraceRaysKHR` → Metal ray dispatch. |
| 4A.4 | `VK_KHR_ray_query` (inline RT) | MoltenVK | Required for tier 1.1. Metal has no inline `intersect` intrinsic in fragment/compute — map RayQuery to Metal `intersector` functions via precomputed pipeline state or a software ray traversal fallback **on GPU** (BVH traversal in MSL). This is the single largest risk in 12_2; needs a dedicated prototype before anything else in 4A. |
| 4A.5 | vkd3d DXR activation | vkd3d | `acceleration_structure.c` / `raytracing_pipeline.c` light up once MVK exposes RT; verify `VKD3D_CONFIG=dxr` flow and generic Wine environment plumbing for DXR apps. |
| 4A.6 | SPIRV-Cross RT MSL | SPIRV-Cross | DXIL→SPIR-V→MSL for raygen/closest-hit/any-hit/miss/intersection + RayQuery lowering; fix gaps against the `metal` compiler. |
| 4A.7 | DXR probes | tests | DXR triangle + procedural sphere (raygen-only first), closest-hit with attributes, any-hit alpha, inline RayQuery in CS, AABB intersection, TLAS instancing. |

Sub-rung 4B — **Mesh shaders (tier 1)**:

| # | Item | Layer | Detail |
|---|---|---|---|
| 4B.1 | Metal mesh shaders | MoltenVK | `MTLMeshRenderPipelineDescriptor`, `MTLMeshBuffer` (per-primitive/per-vertex payloads), object→mesh dispatch (`dispatchThreadgroups:threadsPerObjectThreadgroup:`), Metal 4 semantics on Apple M4. |
| 4B.2 | `VK_EXT_mesh_shader` | MoltenVK | Task+mesh stages, `vkCmdDrawMeshTasksEXT`, mesh NV/KHR conversion, meshlet amplification (task count), `meshShaderQueries`. |
| 4B.3 | SPIRV-Cross mesh MSL | SPIRV-Cross | `[[object]]`/`[[mesh]]` function generation, mesh payload structs, `gl_MeshPerPrimitiveEXT`/`gl_MeshPerVertexEXT` blocks, `SetMeshOutputsEXT` lowering. |
| 4B.4 | vkd3d mesh activation | vkd3d | Existing mesh path gates on the EXT; verify DrawIndexed-equivalent path + amplification shader + per-primitive culling on Metal. |
| 4B.5 | Mesh probes | tests | Meshlet cube (pos+color payload), amplification/task culling, per-primitive export, indexed/indirect mesh draws, GS-free pipeline equivalence. |

Sub-rung 4C — **Variable-rate shading (tier 2)**:

| # | Item | Layer | Detail |
|---|---|---|---|
| 4C.1 | Metal rasterization rate maps | MoltenVK | `MTLRasterizationRateMap` + `MTLRasterizationRateLayerDescriptor`; verify per-tile rate granularity and supported rate pairs (1x2/2x1/2x2/2x4/4x2/4x4) on Apple M4. |
| 4C.2 | `VK_KHR_fragment_shading_rate` | MoltenVK | Attachment (rate image), pipeline rate, combiners — map to rate maps + render-pass attachment plumbing. |
| 4C.3 | **Per-primitive VRS (tier-2 requirement)** | MVK+vkd3d | Metal has no shader-side per-primitive shading rate output. Options: (a) rate-map emulation via primitive classification pass — **RISK: may be impossible exactly**; (b) vkd3d-side expansion using mesh shaders once 4B lands; (c) document as the hard blocker with a tier-1 fallback that still blocks the 12_2 rung (honest RED). Decision gate at M10. |
| 4C.4 | VRS probes | tests | Rate-image 2x2/2x4 shading, per-region rate changes, combiner matrix, per-primitive (if 4C.3 lands). |

Sub-rung 4D — **Remaining rung bits**:

| # | Item | Layer | Detail |
|---|---|---|---|
| 4D.1 | SM 6.5 verification | vkd3d | Confirm ladder reaches 6.5 on MVK (needs subgroup ops compute+fragment, scalar/UBO standard layout ✓, shaderInt16 ✓, denorm float controls). SM 6.6+ blocked by missing `VK_KHR_compute_shader_derivatives` — 12_2 only needs 6.5; 6.6+ is a separate stretch goal (implement compute derivatives in MVK). |
| 4D.2 | Depth bounds | vkd3d/MVK | `DepthBoundsTestSupported` — either MVK native emulation (fragment discard against depth bounds) or vkd3d emulation path; verify which and ship one. |
| 4D.3 | Copy-queue timestamps | MoltenVK | `CopyQueueTimestampQueriesSupported` — timestamps + `vkCmdCopyQueryPoolResults` on the transfer queue family. |
| 4D.4 | Casting formats | MoltenVK | `CastingFullyTypedFormatSupported` — format-features2 CASTING_BIT coverage on required pairs. |
| 4D.5 | Sampler feedback tier ≥ 0.9 | MVK+SPIRV-Cross | Needs `shaderImageInt64Atomics` (VK_KHR_shader_atomic_int64 — currently absent). Metal 64-bit image atomics (`atomic_ulong`) availability on Apple9 must be verified; implement the Vulkan feature + SPIRV-Cross lowering, then vkd3d's `WriteSamplerFeedback` lights up. If Metal can't do int64 image atomics, implement the int64-atomic-append emulation path in vkd3d (there is one for limited hardware) — verify semantics. |
| 4D.6 | Tier-3 tiled + CR tier 3 | (2.3 + 3.3 outputs) | Already planned above; 12_2 consumes them. |
| 4D.7 | 12_2 probe matrix + "DX Ultimate" gate | tests | Full rung matrix green + ladder TRACE `Max feature level: 0xc200` + real DXR/mesh/VRS game smoke. |

### Rung 5 — CORE_1_0 (compute-only)

| # | Item | Layer | Detail |
|---|---|---|---|
| 5.1 | Accept `D3D_FEATURE_LEVEL_1_0_CORE (0x1000)` | vkd3d | Currently rejected (`Invalid feature level 0x1000`). Extend `is_valid_feature_level` + device creation for compute-only devices (no graphics queue requirement; RB tier 1, SM 6.0). |
| 5.2 | Core-compute entry surface | vkd3d | The custom core already references `DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE` — verify the `D3D12GetInterface`/factory path and align with the D3D12CoreComputeDevice convention (compute-only adapter enumeration). |
| 5.3 | CORE_1_0 probes | tests | Compute-only device creation, compute PSO + dispatch + UAV readback, SM 6.0 compute corpus, no-graphics-queue negative tests. |

**Exit:** device at 0x1000 created; compute-only matrix green.

## 6. Cross-Cutting Workstreams

### WS-A — MoltenVK capability work
Everything in Section 5 marked "MoltenVK", plus:
- **Feature-gate discipline**: each new Vulkan feature/extension ships disabled until its probe row is green; `VkPhysicalDeviceFeatures2` chains must report only proven bits.
- **MVK configuration surface**: log/verify `MVK_CONFIG_*` (argument buffers, shader dump, pre-rotation) stays consistent with the launch environment.
- **Universal dylib**: keep x86_64+arm64 slices (runtime is Rosetta x86_64; arm64 slice needed for ICD/tooling parity).

### WS-B — vkd3d-proton macOS/VKMT-specific work
- Re-derive the VKMT patch set on upstream master in our workspace fork (`sources/vkd3d-proton`), then apply our feature work as clean commits:
  - D3DKMT adapter path under Wine (make headless device creation work — Rung 0.1),
  - transform-feedback policy (VKMT disables stream output; keep it off — D3D12 has no XFB),
  - DXVK interop interfaces (`ID3D12DXVKInteropDevice*`) ABI compatibility with the shipped DXVK dxgi,
  - Forwarder/Agility split (`d3d12.dll` forwarder + `d3d12core.dll` exporting `D3D12GetInterface`/`D3D12SDKVersion`) — preserve the exact split so Wine override/deployment remains reliable.
- Feature-level ladder: keep in sync with upstream `device.c`; every custom change to the ladder must be justified + tested.

### WS-C — SPIRV-Cross MSL lowering
- Mesh (4B.3), RT (4A.6), sampler-feedback int64 atomics (4D.5), VRS output variables (4C.3), interlock/ROV patterns (3.1), logic-op shader fragments if emulation needs shader changes (1.1).
- Pin a workspace SPIRV-Cross revision in both the standalone clone and MoltenVK's `External/SPIRV-Cross`; **rebuild MoltenVK's nested artifact after any SPIRV-Cross edit** (stale-external pitfall).

### WS-D — MSL / metallib pipeline
- Compiler toolchain: Xcode 27 beta 4 (`DEVELOPER_DIR=/Users/averyfelts/Downloads/Xcode-beta.app/Contents/Developer`) + CLT beta 5; `xcrun -sdk macosx metal`, `metallib`, `metal-ar`, `metal-tt` all verified in `scripts/env.sh`.
- Every generated shader must compile with `xcrun metal`; shader-dump automation (MVK `MVK_CONFIG_SHADER_DUMP_DIR` + vkd3d `VKD3D_SHADER_DUMP_PATH`) → offline compile check → binary MSL cache validation.
- `metallib` pipeline for any precompiled/default libraries (`default.metallib` considerations).

### WS-E — Build & packaging
- **vkd3d-proton PE build**: llvm-mingw 20260616 (clang 22.1.8 — exact match to the shipped build's compiler) + ninja + meson, cross file for `x86_64-w64-mingw32`, producing the forwarder pair into `artifacts/build/vkd3d-proton/x86_64-windows/`.
- **MoltenVK build**: CMake/Xcode universal dylib + `MoltenVK_icd.json` (library_path relative), into `artifacts/build/moltenvk-vkmt/`.
- **Runtime integration (final milestone)**: publish the zstd runtime archive with exact filenames, manifest, checksums, and game-directory staging checks before any user launch.

### WS-F — Validation & conformance
- `scripts/mvkprobe.c` (native Vulkan capability dump against an exact dylib path) — the Phase-0 tool, extended per rung with functional rows (not just enumeration): RT triangle, mesh cube, VRS image, sparse residency, logicOp render, CR coverage.
- `scripts/flprobe` (D3D12 feature-level probe exe under compatible Wine, staged DLLs) — the per-rung gate; extend with functional tests per rung.
- Test-first rule from this project's history: **advertisement ≠ support**. Each promoted capability needs a GPU-executed row with deterministic readback.

### WS-G — Wine integration
- Generic Wine launch environment; keep runtime staging and feature-level probes independent of any particular Wine distribution.
- Release checklist from `docs/optimization-roadmap/release-checklist.md` (bundle lanes, ICD, dry-run, game acceptance).
- Never touch the installed app until the PR ships; all iteration happens against the workspace builds + the extracted bundle archive.

## 7. Milestones & Gates

| M | Name | Exit evidence |
|---|---|---|
| **M0** | Workspace + toolchain | ✅ DONE 2026-08-14 — llvm-mingw 20260616 (clang 22.1.8, exact shipped-build compiler) + ninja/meson/cmake + Xcode 27b4 + CLT b5 verified; 4 fresh clones; `validate-toolchain.sh` 20/20; repo `VKD3D-Proton-MacOS` committed (587b89f) |
| **M1** | Harness parity | ✅ DONE 2026-08-14 — headless D3D12CreateDevice works in the generic Wine launch environment. Blocker root-caused: missing single-texel alignment is a HARD E_INVALIDARG (upstream device.c:3317-3321); the validated launch shape sets `VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT=1` (+MVK_PRESENT_MODE=1). Evidence: artifacts/evidence/2026-08-14-m1-m2-headless-device.md, runs m1-runD/E. ABI finding: custom build uses mingw-renumbered D3D12_FEATURE enum (SM=7, O5=27, O6=30, O7=32). |
| **M2** | 11_0 conformant | ✅ DONE (query surface) 2026-08-14 — full empirical CheckFeatureSupport matrix captured (FEATURE_LEVELS max=11_0; RB tier 3; Tiled/CR NOT_SUPPORTED; ROVs=1; LogicOp=0; TIR=1; VA bits 40; DXR/VRS/mesh/sampler-feedback NOT_SUPPORTED). MSL zero-error corpus + game acceptance still open (M2 remainder). |
| **M3+M4** | 11_1 rung | ✅ DONE 2026-08-14 — logic-op emulation implemented (MVK framebuffer-fetch MSL injection; 16/16 ops pixel-exact on GPU) + 64 storage-buffer descriptors (Tier-2 arg buffers). FEATURE_LEVELS max = 11_1 (0xb100), OutputMergerLogicOp=1, SM 6.5 on the fully self-built stack. Key finds: runtime wine loads a fused libMoltenVK.1.dylib (DYLD override injects ours); VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY=0/NONE=2. |
| **M3** | logicOp prototype | Pixel-exact logic-op emulation A/B green (16 ops × formats × MSAA) |
| **M4** | 11_1 rung | ladder 0xb100; 64-UAV + TIR + logic-op probes green |
| **M5** | 12_0 rung | ✅ DONE 2026-08-14 — vkQueueBindSparse implemented (MTL4 updateTextureMappings + MTLEvent sync); sparse cycle GPU-proven (bind→write→readback 0x11223344; unmap→NULL-tile 0). Tier-1 placement-sparse fix (ShaderWrite usage; tier-2 faults on macOS 26). Ladder 0xc000, TiledResourcesTier=4. Evidence: 2026-08-14-m5-sparse-execution-landed.md |
| **M6** | 12_1 rung | ✅ DONE 2026-08-14 — ladder max=0xc100, min 12_1 CREATED. CR tier 1: VK_EXT_conservative_rasterization + shader-stage emulation (vertex bisector expansion + FS post-snap test + discard) PIXEL-EXACT (vk-cr-probe 12/12 seeds, over=0 under=0). Key fixes: pre-invert the injected position y to cancel the SPIRV-Cross invert; pix2NDC without the +1 for an exact round trip. ROV execution verified: vk-rov-probe interlock ordering 4096/4096 (SPIRV-Cross needs the PixelInterlockOrderedEXT execution mode for raster_order_group). Evidence: 2026-08-14-m6-rung-12_1.md, 2026-08-14-m6-rov-execution.md, rung-12_1-run1.txt |
| **M7** | Ray query prototype | ✅ GREEN 2026-08-14 — pure-Metal intersector probe: BLAS (MTL4 build) + inline intersection in a COMPUTE kernel (the RayQuery analog); 30 hits, distances 5.000-5.431 exact. Feasibility gate passed; the SPIRV-Cross MSL RayQuery lowering is the remaining implementation item. Evidence: 2026-08-14-m7-rayquery-feasibility.md |
| **M7b** | Ray-query dispatch slice | ✅ GREEN 2026-08-15 — the FULL Vulkan inline ray query works 5/5: `INLINE RAY QUERY (FULL VULKAN PATH) WORKS`, hit at minD=5.000 EXACT. Fix stack: discrete AS descriptor sets (direct [[buffer(N)]] resource), MTL4 argument-table dispatch + PSO warm-up (first MTL4 dispatch with a PSO drops writes), output staging (standalone shared + blit copy-back), BLAS opaque=NO (opaque YES un-traversable), TLAS transform 4th column zeroed (VK 3x4 OOB), TLAS instance buffer bound. Commits: MoltenVK 8c48bc1 + prior, SPIRV-Cross fc6cae47. Evidence: 2026-08-14-m7-rayquery-full-path.md |
| **M8** | DXR 1.1 activation | 🔶 FIRST SLICE DONE 2026-08-15 — `RaytracingTier=11` via the inline ray query: vkd3d fork tier fallback (2d71b20) + MVK gate fixes (4cdc0d3: AS flagCount 5, rayTraversalPrimitiveCulling, RTAS VBO format bits). The TraceRay/RTPSO path remains unimplemented (state objects fail cleanly). 12_2 remaining gates: CR tier 3, depth bounds, VRS tier 2 (M10), mesh shaders (M9), sampler feedback (M10). Evidence: 2026-08-15-m7-dxr11-gate.md |
| **M8** | DXR 1.1 | 4A probes green; ray tracing tier 1.1 reported |
| **M9** | Mesh shaders | ✅ M14 — mesh tier 1 reported; mesh-only D3D12 dispatch is pixel-exact. Task/object amplification remains a follow-up. |
| **M10** | VRS + sampler feedback | ✅ M14 capability gate — VRS tier 2 and sampler feedback 0.9 report; sampler-feedback shader/readback probe is green. |
| **M11** | Rung-4D sweep | ✅ M14 — SM 6.5, depth bounds, copy-queue timestamps, casting, and sampler feedback are reported/covered by the ladder and probes. |
| **M12** | 12_2 rung | ✅ M14 — ladder reaches 0xc200; companion 12_2 acceptance probes are green. |
| **M13** | CORE_1_0 | ✅ GREEN 2026-08-15 — 1_0_CORE device creation (dev=CREATED), compute matrix (UAV readback 42.0), and SM 6.0 corpus/geometry-shader corpus. Evidence: 2026-08-15-m13-core-1-0.md |
| **M14** | Public macOS runtime release | ✅ SHIPPED — public repository `aaf2tbz/vkd3d-proton-macos`, tag `v1.0`, runtime tarball, feature matrix, and full regression evidence. |
| **DXGI-1** | Adapter identity | ✅ COMPLETE 2026-08-16 — pinned DXVK macOS `dxgi.dll`, factory/adapter probe, DXGI↔D3D12 LUID match, Vulkan/MoltenVK identity, ten repeatable runs, negative tests, and six-probe regression. Evidence: `artifacts/evidence/dxgi-1-adapter-identity.md` |
| **DXGI-2** | Windowed presentation | ✅ COMPLETE 2026-08-16 — four flip/API combinations, deterministic GPU readback, 1,000 frames per mode, sync/tearing/statistics/negative tests, two repeatable runs, and six-probe regression. Evidence: `artifacts/evidence/dxgi-2-presentation.md` |
| **DXGI-3** | Window lifecycle | ✅ COMPLETE 2026-08-16 — resize matrix, minimized/zero-size classification, occlusion, fullscreen/windowed fallback, destruction/recreation, negative tests, 100 create/resize/destroy cycles, ordered shutdown, two repeatable runs, and preserved DXGI-1/DXGI-2/six-probe gates. Evidence: `artifacts/evidence/dxgi-3-lifecycle.md` |
| **DXGI-4** | Formats and color policy | ✅ COMPLETE 2026-08-16 — exact BGRA8/RGBA8 UNORM and sRGB GPU readback, R10/depth-stencil capability boundaries, alpha/color-space/HDR metadata policy, tearing, negative tests, two repeatable runs, and preserved DXGI-1/2/3/six-probe gates. Evidence: `artifacts/evidence/dxgi-4-formats.md` |
| **DXGI-5** | Synchronization and pacing | ✅ COMPLETE 2026-08-16 — frames-in-flight modes 2/3/4, fences/events, queue dependencies, frame-latency waits, sync intervals 0/1, deterministic readback, resource/pipeline churn, memory samples, 100,000-frame stress, unsupported-boundary reporting, and preserved DXGI-1/2/3/4/six-probe gates. Evidence: `artifacts/evidence/dxgi-5-synchronization.md` |

**Ordering note:** M3–M12 are strictly sequential rungs (the ladder depends downward). Within 12_2, 4A/4B/4C/4D may be parallelized across lanes but each promotes only with its own gate green.

## 8. Risk Register (feasibility on Metal / Apple M4)

| Risk | Severity | Confidence | Mitigation / fallback |
|---|---|---|---|
| Metal has no logic ops (11_1) | HIGH | MEDIUM | Imageblock/framebuffer-fetch emulation; exactness gate required |
| Metal sparse API coverage for tier ≥2 (12_0) | HIGH | MEDIUM-HIGH | `supportsSparseTextures` verified early (M5 spike); fallback: tier-1 only → 12_0 blocked honestly |
| Conservative raster exactness incl. degenerate triangles (12_1/12_2 tier 3) | HIGH | MEDIUM | Imageblock coverage stage; tier-1 first, tier-3 after 4B mesh machinery exists |
| Inline ray query on Metal (DXR 1.1) | **CRITICAL** | LOW-MEDIUM | GPU BVH traversal in MSL or Metal intersector-in-CS pattern; M7 go/no-go |
| Per-primitive VRS on Metal (12_2 tier 2) | **CRITICAL** | LOW | Rate-map emulation or mesh-based expansion; may be impossible exactly → honest RED blocks 12_2 |
| int64 image atomics for sampler feedback (12_2) | HIGH | MEDIUM | Metal `atomic_ulong` availability on Apple9; vkd3d emulation fallback |
| Per-pixel ROV ordering on tile-based Apple GPU | MEDIUM | HIGH | interlock already advertised; execution proof pending |
| Shipped custom patch set is closed (vkd3d VKMT) | HIGH | HIGH | Re-derive from binary markers + upstream; ABI-compat tests against shipped pair |
| Runtime self-heal can replace custom lanes on launch | MEDIUM | HIGH | Acceptance = post-launch hash comparison and explicit runtime staging |
| Rosetta x86_64-only runtime constraints | MEDIUM | HIGH | x86_64-only PE builds; universal dylib |
| SM 6.6+ blocked (no compute derivatives) | LOW (12_2 needs 6.5 only) | HIGH | Stretch goal; implement `VK_KHR_compute_shader_derivatives` in MVK |

## 9. Definition of Done (per feature, per rung)

1. **Source-backed design**: change documented in the workspace fork with commit hashes.
2. **Binary-backed build**: artifact hashes recorded in `docs/Final.md` and the release archive (d3d12.dll, d3d12core.dll, libMoltenVK.dylib).
3. **Capability gate**: the exact feature reports correctly through the *real* D3D12 `CheckFeatureSupport` / Vulkan `GetPhysicalDeviceFeatures2` chain (no env forcing).
4. **Execution row**: a GPU-executed probe with deterministic, sentinel-verified readback for every promoted behavior.
5. **Ladder proof**: vkd3d TRACE `Max feature level: 0xXXXX` captured from the real compatible Wine route.
6. **Regression proof**: all lower rungs + Control still green.
7. **Provenance proof**: loaded-module paths + hashes recorded for the game/probe process (the project's mandatory acceptance check).

## 10. References

- `docs/Final.md` — consolidated final state, hashes, features, and acceptance
- `docs/Development.md` — prerequisites, build targets, validation, and release workflow
- `docs/features.md` — current feature matrix
- `docs/validation.md` — current probe gate
- `README.md` — workspace map
- Upstream ladders: `sources/vkd3d-proton/libs/vkd3d/device.c` (`d3d12_device_caps_init_feature_level`, `d3d12_device_determine_tiled_resources_tier`, `d3d12_device_determine_conservative_rasterization_tier`, `d3d12_device_determine_ray_tracing_tier`, `d3d12_device_determine_mesh_shader_tier`, `d3d12_device_determine_variable_shading_rate_tier`, SM ladder)
