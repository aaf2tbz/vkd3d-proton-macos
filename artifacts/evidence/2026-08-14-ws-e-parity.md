# WS-E Evidence — Self-built pair achieves behavioral parity (2026-08-14)

Build: upstream master + 3 VKMT-style portability patches on branch
vkd3d-proton-macos/work:
  1. transform feedback hard-fail -> WARN (device.c)
  2. single texel alignment hard-fail -> VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT (device.c)
  3. NV memory-decompression meta pipelines gated on VK_NV_memory_decompression (meta.c)

Artifacts (llvm-mingw clang 22.1.8, meson+ninja, --buildtype release --strip):
  d3d12.dll     450,560 B  PE32+ x86-64
  d3d12core.dll 6,459,392 B PE32+ x86-64

Probe (flprobe, staged with DXVK dxgi + runtime ICD + VKMT env, Wine 11.5):
  D3D12CreateDevice: 11_0 -> S_OK CREATED; 12_2/12_1/12_0/11_1/CORE_1_0 -> E_INVALIDARG
  FEATURE_LEVELS: max = 11_0 (0xb000)
  OPTIONS: RB tier 3, Tiled NOT_SUPPORTED, CR NOT_SUPPORTED, ROVs=1,
           OutputMergerLogicOp=0, TIR=1, VA bits=40, Heap tier 2, PSStencilRef=1
  SHADER_MODEL: official id 18 -> E_INVALIDARG; mingw id 7 -> S_OK (highest=0)
  OPTIONS5/6/7: official ids 19/20/21 -> E_INVALIDARG; mingw ids 27/30/32 -> S_OK
                (DXR tier 0, VRS tier 0, mesh tier 0, sampler feedback 0)
  -> IDENTICAL to the shipped pair (evidence m1-runD/E). ABI note: the
     mingw-renumbered D3D12_FEATURE enum is a build-toolchain consequence
     (llvm-mingw ships mingw-w64 headers), confirmed by reproducing it.
