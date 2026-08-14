# M4 Analysis — 64-UAV Descriptor Limit (11_1 rung)

> Source-grounded analysis by Luna (mswr.luna-executor, gpt-5.6-luna), 2026-08-14.
> Suggested destination per Luna; materialized by the orchestrator.

## M4 descriptor-limit analysis

Suggested destination: `docs/05-64-uav-descriptor-limit-analysis.md`.

### Current limit source

`MVKDevice.mm`:

- `:2458` sets `_metalFeatures.maxPerStageBufferCount = 31`.
- `:2923-2929` maps that value directly to:
  - uniform buffers;
  - storage buffers;
  - sampled/storage resources;
  - `maxPerStageResources`.
- `:2935-2936` derives descriptor-set storage-buffer limits from the same value.
- `:4025-4031` uses the direct 31-buffer value for variable storage-buffer descriptor counts.

The 31 value is a direct Metal buffer-slot limit and must not be changed globally. It is also used by:

- `MVKPipeline.mm:2555-2556` for implicit-buffer indices;
- direct descriptor binding bounds in `MVKPipeline.mm:266-337`;
- `MVKDevice.mm:4894`;
- pipeline resource accounting.

### Argument-buffer path

The existing argument-buffer implementation can represent 64 storage-buffer descriptors:

- `MVKDescriptorSet.mm:166-224` selects argument-buffer mode for normal descriptor sets.
- `:384-414` maps storage buffers to `BufferAuxSize` when argument buffers are active.
- `:460-515` creates `MTLDataTypePointer` argument arrays with `setArrayLength=count`.
- `MVKPipeline.mm:157-217` indexes resources inside argument buffers.
- `:365-395` clears internal argument-buffer resources from direct counts and reserves descriptor-set argument-buffer slots.
- `MVKPipeline.mm:266-337` only applies the 31 limit to the non-argument-buffer direct path.

### Minimal safe change

Use a separate logical Vulkan descriptor limit:

```cpp
const bool tier2ArgumentBuffers =
    _isUsingMetalArgumentBuffers &&
    (_metalFeatures.argumentBuffersTier >= MTLArgumentBuffersTier2);

const uint32_t maxStorageBufferDescriptors =
    tier2ArgumentBuffers ? 64u : _metalFeatures.maxPerStageBufferCount;
```

Use this value for:

- `maxPerStageDescriptorStorageBuffers`;
- `maxDescriptorSetStorageBuffers`;
- `maxDescriptorSetStorageBuffersDynamic`;
- variable storage-buffer descriptor support.

Keep `_metalFeatures.maxPerStageBufferCount` at 31 for direct Metal resources and implicit buffers.

`getMaxPerSetDescriptorCount()` currently returns at least 1024 (`MVKDevice.mm:3501-3505`), so the conservative target of 64 fits. Do not advertise the much larger 1M argument-buffer update-after-bind values as core descriptor limits without a separate audit.

### Required additional fix

`MVKDevice.mm:4025-4031` currently computes variable storage-buffer capacity from the direct 31-slot value. It must use the logical argument-buffer limit when Tier 2 argument buffers are active.

### Blocking caveat

`MVKDescriptorSet.mm:191-193` forces push-descriptor layouts to `ArgumentBufferMode::Off`. Therefore globally advertising 64 storage-buffer descriptors is not correct until one of these is addressed:

1. push descriptors gain an argument-buffer-backed update path; or
2. the supported route explicitly excludes push-descriptor layouts and prevents them from reaching the promoted capability.

A fixed 64-descriptor regular descriptor-set prototype is technically plausible, but it cannot be promoted as a general Vulkan limit while this direct-path caveat remains unresolved.

### Required GPU validation rows

- physical properties report 64;
- fixed 64-storage-buffer descriptor set creation/update;
- bind and draw with all 64 descriptors referenced;
- dynamic-storage-buffer variant;
- variable descriptor-count variant;
- mixed descriptor types;
- pipeline recreation/cache hit;
- push-descriptor negative test;
- non-Tier-2/direct path remains limited to 31.

No M4 source prototype or GPU row was produced in this session.

## Evidence and repository state

No new files were created under `artifacts/evidence/`, and `docs/01-feature-level-evidence.md` was not updated. No workspace or nested MoltenVK commits were made.