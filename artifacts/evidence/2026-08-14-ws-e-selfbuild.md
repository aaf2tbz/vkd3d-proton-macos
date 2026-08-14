# WS-E Evidence — Self-built vkd3d-proton (llvm-mingw) + VKMT patch derivation

## Build pipeline (validated end-to-end)
- Toolchain: llvm-mingw 20260616 (clang 22.1.8), ninja 1.11.1, meson, widl from the
  MetalSharp wine runtime bin (no brew widl needed).
- Cross file: artifacts/vkd3d-cross-x86_64.txt (x86_64-w64-mingw32-clang etc.).
- Command: meson setup --cross-file ... --buildtype release --strip -Denable_trace=false
  + ninja. 206/206 targets. Outputs:
    d3d12.dll     450,560 B  PE32+ x86-64   sha 3a4f0ea9…
    d3d12core.dll 6,459,392 B PE32+ x86-64  sha 4ca20a70…
  (shipped pair: 446,464 / 6,434,816 — same shape, upstream master)

## VKMT patch derivation (the shipped custom build's relaxations)
Upstream master hard-fails D3D12CreateDevice on this MoltenVK at THREE points:
1. missing transform feedback: device.c ERR("Lacking support for transform feedback.") -> E_INVALIDARG
   -> shipped logs WARN "stream output is disabled (VKMT)" and continues. PATCHED in our fork.
2. missing single texel alignment: device.c ERR -> E_INVALIDARG
   -> shipped honors VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT=1. PATCHED in our fork.
3. (device init, after caps) vkd3d internal region-buffer meta shader fails MSL compile:
   program_source:114: error: address of vector element requested
     atomic_fetch_add_explicit((device atomic_uint*)&region_buffer->region_count[0u], ...)
   -> MoltenVK VK_ERROR_INITIALIZATION_FAILED (E_FAIL). The shipped custom MoltenVK
   1.4.2 compiles this fine -> its embedded SPIRV-Cross carries the fix. OUR MoltenVK
   fork must re-derive it (SPIRV-Cross MSL atomic-on-array-member codegen).

## Probe against self-built pair (stage-self, upstream master + patches 1-2)
- exit 1: all levels E_FAIL at device init due to blocker 3 (MSL meta shader).
- Patches 1-2 verified working: caps init passes (no more E_INVALIDARG from texel/xfb).
