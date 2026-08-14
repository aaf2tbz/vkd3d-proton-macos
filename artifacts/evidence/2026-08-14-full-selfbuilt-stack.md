# Full self-built stack verified end-to-end (2026-08-14)

Stack (100% built from source in this workspace):
- vkd3d-proton: upstream master + 3 VKMT-style patches (branch vkd3d-proton-macos/work),
  llvm-mingw 20260616 (clang 22.1.8) PE cross build
- MoltenVK: fresh Khronos clone (f9a1e96) + fetchDependencies --macos (SPIRV-Cross/SPIRV-Tools
  XCFrameworks) + `make macos` under Xcode 27 beta 4 (27A5228h); universal dylib
  sha 52ba64c7… (10,984,784 B), Vulkan 1.4.357 instance / 1.3.357 device, 134 device extensions
- dxgi.dll: DXVK from the shipped bundle (18,940,247 B)
- Runtime: MetalSharp Wine 11.5 + metalsharp-wine env (VK_ICD_FILENAMES, DYLD,
  MVK_PRESENT_MODE=1, VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT=1)

Result (flprobe, evidence full-selfbuild-run1.txt, exit 0):
- D3D12CreateDevice: 11_0 -> S_OK CREATED; 12_2/12_1/12_0/11_1/CORE_1_0 -> E_INVALIDARG
- FEATURE_LEVELS max = 11_0 (0xb000)
- OPTIONS: RB tier 3, Tiled NOT_SUPPORTED, CR NOT_SUPPORTED, ROVs=1, LogicOp=0, TIR=1,
  VA bits 40, Heap tier 2, PSStencilRef 1
- SHADER_MODEL / OPTIONS5/6/7: mingw-renumbered ids (7/27/30/32) S_OK, official ids E_INVALIDARG
  (ABI matches shipped pair)
- Identical to shipped stack on the full query surface.

MoltenVK deltas vs shipped custom (50e41de2…):
- 134 vs 130 device extensions (newer upstream); drawIndirectCount 1 vs 0 (VKMT disabled it;
  re-apply if needed). No functional regression for the feature-level ladder.
