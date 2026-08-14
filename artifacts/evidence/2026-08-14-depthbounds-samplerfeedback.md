# Depth bounds + sampler feedback: hardware truth (2026-08-14)

## Depth bounds test
- Metal API exists: -[MTLRenderCommandEncoder setDepthTestMinBound:maxBound:] (macOS 26+)
- MVK gates features.depthBounds on supportsMTLGPUFamily(Apple10)
- PROVEN on Apple M4 (Apple9): calling the API TRAPS (Trace/BPT) -> hardware
  requires Apple10+ (probe: scripts/probes/metal-depthbounds-probe.mm; baseline
  render works, bounds call traps)
- => DepthBoundsTestSupported=0 on M4. Paths: (a) shader-emulation in MVK
  (fragment discard vs interpolated depth + implicit depth-bounds buffer; PS-runs-
  then-discards deviation; [[position]] collision handling needed) - DESIGN ONLY
  so far; (b) Apple10+ hardware. 12_2 ladder requires this option = 1.

## Sampler feedback (12_2 ladder: SamplerFeedbackTier >= 0.9)
- vkd3d: requires shaderInt64 && shaderImageInt64Atomics (VK_EXT_shader_image_atomic_int64)
- Metal: 64-bit atomics DO NOT COMPILE (atomic_ulong rejected by metal compiler)
  -> MVK cannot expose shaderImageInt64Atomics
- Paths: (a) vkd3d fork reimplements the sampler-feedback encode/decode meta-shader
  pair with a 32-bit-atomic encoding (ABI-consistent between WriteSamplerFeedback
  and DecodeSamplerFeedback; vkd3d controls both sides) - real project, gated;
  (b) leave NOT_SUPPORTED (12_2 blocked).
- Note: vkd3d also gates options14 AdvancedTextureOps on int64 image atomics
  (SM 6.7-era, out of scope).

## CORE_1_0 scope (vkd3d-side)
- is_valid_feature_level (utils.c:1086) lacks 0x1000; vkd3d_main.c:46 rejects < 11_0
- Work: accept 1_0_CORE; compute-only device creation (no graphics queue);
  FEATURE_LEVELS query handling; DXCORE core-compute adapter attributes (custom
  build already carries DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE strings).
