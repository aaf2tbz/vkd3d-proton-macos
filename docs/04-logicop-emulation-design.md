# M3 Design — logicOp Emulation (11_1 blocker)

> Source-grounded analysis by Luna (mswr.luna-executor, gpt-5.6-luna), 2026-08-14,
> based on workspace sources: MoltenVK `f9a1e964`, SPIRV-Cross `9c3c8e2`,
> shipped custom MoltenVK `50e41de2…`. Materialized into the workspace by the orchestrator.

## M3 design draft: logicOp emulation

Suggested destination: `docs/04-logicop-emulation-design.md`.

### Current blockers

- `MVKDevice.mm:2797` sets `VkPhysicalDeviceFeatures.logicOp` from `useMetalPrivateAPI`.
- `MVKPipeline.mm:1989-1993` uses Metal’s private `logicOperationMVK` API.
- Both are unsuitable for the approved route.
- `SPIRV-Cross/spirv_msl.hpp:377-378` has `use_framebuffer_fetch_subpasses`, but it defaults false.
- `SPIRV-Cross/spirv_msl.cpp:15345-15370` emits `[[color(n)]]` framebuffer-fetch parameters only for `DimSubpassData` when that option is enabled.
- MoltenVK does not currently set that option.
- `MVKImage.mm:958-960` explicitly documents that framebuffer fetch is unsupported for memoryless input attachments.

### Proposed implementation

1. Keep `logicOp=false` until the exact GPU gate passes.
2. Add internal conversion metadata containing:
   - logic operation;
   - affected color attachments;
   - attachment formats;
   - write masks;
   - sample count and sample-rate mode.
3. Include this metadata in the shader/pipeline cache identity.
4. In the fragment conversion path:
   - fetch the destination attachment in the same render pass;
   - compute the logic operation on the exact stored representation;
   - disable fixed-function blending for affected attachments;
   - preserve write masks and unaffected channels.
5. Use framebuffer fetch where exact representation is proven.
6. Use Metal imageblock/tile storage where typed framebuffer fetch loses raw bits or cannot provide per-sample behavior.
7. Require raster-order/interlock semantics where needed. Existing source maps `fragmentShaderSampleInterlock` and `fragmentShaderPixelInterlock` from `_metalFeatures.rasterOrderGroups` at `MVKDevice.mm:732-736`.

Relevant pipeline integration points:

- `MVKPipeline.mm:1170-1206`: render pipeline construction.
- `MVKPipeline.mm:1652-1705`: fragment shader conversion and sample-rate handling.
- `MVKPipeline.mm:1943-2000`: color formats, write masks, blending, and current private logic-op path.
- `MVKRenderPass.mm` and `MVKCommandBuffer.mm`: same-pass attachment handling and restart behavior.

### Logic operation table

For each channel, with result masked to the channel width:

| Operation | Result |
|---|---|
| CLEAR | `0` |
| SET | all ones |
| COPY | `src` |
| COPY_INVERTED | `~src` |
| NO_OP | `dst` |
| INVERTED | `~dst` |
| AND | `src & dst` |
| NAND | `~(src & dst)` |
| OR | `src \| dst` |
| NOR | `~(src \| dst)` |
| XOR | `src ^ dst` |
| EQUIVALENT | `~(src ^ dst)` |
| AND_REVERSE | `src & ~dst` |
| AND_INVERTED | `~src & dst` |
| OR_REVERSE | `src \| ~dst` |
| OR_INVERTED | `~src \| dst` |

### Exactness matrix

The functional gate must cover all 16 operations ×:

- `R8G8B8A8_UNORM`;
- `R8G8B8A8_SRGB`;
- `R16G16B16A16_SFLOAT`;
- `R32G32B32A32_SFLOAT`;
- packed `R10G10B10A2` layouts;
- 1× and 4× MSAA;
- all write masks and overlapping fragments.

The implementation must operate on the exact attachment representation, not merely numerically equivalent floats. In particular:

- sRGB must operate on encoded 8-bit values, not linearized values;
- packed 10/10/10/2 channels require exact pack/unpack;
- float rows must test NaNs, signed zero, subnormals, and payload bits;
- MSAA must validate individual samples, not only a resolved image.

Rows that are illegal or unsupported by the relevant Vulkan/D3D12 format rules must be reported explicitly and must not be silently omitted.

### GPU evidence gate

Each row must:

1. seed the destination on the GPU;
2. render the source fragment on the GPU;
3. perform the emulated logic operation;
4. read back deterministic bytes from the GPU;
5. compare against a CPU-computed reference;
6. record candidate hashes, source revisions, GPU identity, OS, format, sample count, operation, and readback bytes.

Any failure leaves `logicOp=false`. No CPU-rendering fallback, private Metal API, or feature-forcing environment variable is acceptable.

## M4 descriptor-limit analysis
