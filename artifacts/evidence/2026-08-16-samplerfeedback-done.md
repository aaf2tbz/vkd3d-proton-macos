# Slice 3 DONE: Sampler feedback 64-bit image-atomic lowering (2026-08-16)

The D3D12 sampler-feedback write path now works end-to-end on the self-built
stack: the dxc `WriteSamplerFeedback` shader → vkd3d DXIL→SPIR-V conversion →
the MVK SPIRV-Cross 64-bit image-atomic emulation → Metal texel atomics on the
2x32 feedback view → the R32G32_UINT readback matches the CPU reference
EXACTLY (3/3 consecutive runs).

## The failure chain before this work (three independent bugs)

1. **The vkd3d gates** (extension + `VK_EXT_shader_image_atomic_int64`
   feature) were the first blocker; the 64-bit atomics on Metal were exposed
   but the ladder required the sampler-feedback tier, and the generated MSL
   did not compile.

2. **The MSL emulation emitted broken code** (SPIRV-Cross `emit_atomic_func_op`
   / `OpGroupNonUniformBitwiseOr` patch):
   - The 64-bit simd-OR statement had two unclosed parens
     (`((uint64_t)(simd_or(...))) | ((uint64_t)(simd_or(...)) << 32u));` →
     the corrected balanced form `(uint64_t)simd_or(...) | (((uint64_t)simd_or(...)) << 32u)`).
   - The atomic emulation assumed a direct-image operand with
     coord=op1/value=op2; the actual SPIR-V is `OpImageTexelPointer`
     (`img@coord`) + `OpAtomicOr` with op1 = the value and op2 unused, so
     `to_expression(op2)` produced the stale temp `_0` and the whole texel
     call was malformed.
   - The patch did not `return` after emitting the replacement, so the
     upstream 32-bit atomic emission ALSO ran and concatenated a second
     `img.atomic_fetch_or(...)` onto the result expression.
   - The fix: split the `img@coord` virtual expression at `@`, use op1 as the
     value, emit `img.atomic_fetch_or(coord, uint4(low, high, 0u, 0u))`
     (the MSL signature is `vec<T,4> atomic_fetch_or(uint2, vec<T,4>)`),
     reconstruct the 64-bit result from `.y/.x`, and return before the
     upstream path.

3. **The descriptor-array element type dropped `access::read_write`**:
   `fixup_image_load_store_access()` marks every storage image
   non-writable/non-readable, and the image-atomic path only loosens the
   DIRECT backing variable of the texel pointer. In the feedback shader the
   atomic image is reached through an ARRAY ELEMENT + a FUNCTION PARAMETER
   (`m_19[base+1]` → `WriteFeedback` param), so the array variable %19 kept
   `NonWritable|NonReadable` and the MSL type became `texture2d<uint>`
   (sample access) while the function parameter was `texture2d<uint,
   access::read_write>` — the call site failed with "no matching function".
   The fix (fork patch `fixup_image_load_store_access_atomic_aware`): scan
   the raw SPIR-V (skipping the 5-word module header) for every
   `OpImageTexelPointer`, resolve the image operand to its base variable
   through access chains AND function-call arguments, and keep those storage
   images unrestricted (read-write).

## Probe fixes (not stack bugs)

- The feedback resource must use `ID3D12Device8::CreateCommittedResource2`
  with a `D3D12_RESOURCE_DESC1` carrying a valid `SamplerFeedbackMipRegion`
  (POT, >= 4x4, <= half the texture; the vkd3d validates it and the plain
  DESC path zeroes it).
- The corpus probe reused fence signal values (1/2/3) across all five tests;
  a monotonic D3D12 fence never goes back, so tests 2-5 waited on
  already-passed values and read stale data. The probe now uses strictly
  increasing per-test signal values (`fence_seq`).

## Acceptance evidence

```
$ corpus.exe    → RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
$ corpus_gs.exe → RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
$ cr_inner_probe.exe → fragments=1691 fc=1691 inconsistent=0
                     RESULT: CR TIER 3 INNERCOVERAGE WORKS (D3D12 PATH)
$ compute_matrix.exe → RESULT: CORE_1_0 COMPUTE MATRIX WORKS
$ feedback_probe.exe → feedback texels written: 8 / 4096
                     RESULT: SAMPLER FEEDBACK MATCHES THE CPU REFERENCE EXACTLY
```

The written feedback map (deterministic, 3/3 runs):
```
[ 0, 0] 0000000f0000000f   [ 1, 0] 0000000f0000000f
[ 2, 0] 0000000f0000000f   [ 3, 0] 0000000f0000000f
[ 0, 1] 0000000f0000000f   [ 1, 1] 0000000f00000007
[ 2, 1] 0000000f0000000f   [ 3, 1] 0000000f0000000f
```
i.e. the 4x2 top-left feedback block carries the lod-8 usage mask 0xF at
bits 32..35 plus the lod-0 usage mask (0xF, except the single asymmetric
texel [1,1] with 0x7); all other 4088 texels are zero. The values follow the
D3D12 SAMPLER_FEEDBACK_MIN_MIP encoding `(region-usage-bits << (lod*4))`
OR-accumulated by the texel atomics.

## Files
- `scripts/patch-spirv-cross.sh`: the corrected 64-bit atomic emulation
  (simd-OR balance, `img@coord` split, op1-as-value, early return) + the
  atomic-aware storage-image access fixup (`fixup_image_load_store_access_atomic_aware`).
- `scripts/probes/feedback/feedback_probe.c`: DESC1 + CreateCommittedResource2
  + the exact CPU-reference readback check.
- `scripts/probes/core10/corpus.c`: strictly-increasing fence signal values.
