# DXGI-4 format, color-space, HDR, and presentation-policy evidence

- Date: 2026-08-16T22:24:32Z
- DXVK source commit: 8f1e28deed3ad30802f7e1bdff428ec14e6e7817
- DXVK bridge patch SHA-256: 366dbead003bec2e58fae03e138b82d01f3fece36af329a5c5976e20e2deeec1
- DXGI-4 patch: /Volumes/AverySSD/VKD3D-Proton-MacOS/patches/dxvk-macos-dxgi-phase4.patch
- DXGI-4 patch SHA-256: 894ace3c52e59a8ba4327c5bc4d502d798b90e0f1f15019dbf57fbed1a2261ee
- vkd3d-proton source revision: 5d24bc718560d6019fc2d74a41981be87bb2d9bd
- MoltenVK source revision: 13e3c967fd14e0dd8a00f456fd218380efbce73c
- Wine runner: /tmp/run-probe.sh
- Wine runtime: wine-11.5
- Host: 27.0
- Compiler: clang version 22.1.8 (https://github.com/llvm/llvm-project.git ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)

## Staged modules and hashes
```text
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
4427de7af7f7ad19973f5cd804ea8fe085bdf59429aac62f4206801a252fac14  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
ac2b8674798bdbdd21ce1aa48daf1e2657813ecc878b80e2641bf0d2c3f2a43e  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
78ab917a20dbc050ba3d0def8c0241e53c90ded0a036462955108e0ef78022a8  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib: Mach-O universal binary with 2 architectures: [x86_64:Mach-O 64-bit dynamically linked shared library x86_64] [arm64]
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib (for architecture x86_64):	Mach-O 64-bit dynamically linked shared library x86_64
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib (for architecture arm64):	Mach-O 64-bit dynamically linked shared library arm64
38e0a7c3839390d524a3bb4b1165d13e96a2c3e771a14df2510c1ad5ab598bde  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/MoltenVK_icd.json
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/MoltenVK_icd.json: JSON data
578ff08cd0d8734619357541771a5abc9c3470ca300030219a971a9e9dbbe466  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/MoltenVK_icd.json
```

## Acceptance excerpt
```text
selected adapter: vendor=0x106b device=0x1b000209 luid=00000000-000003f9
D3D12 adapter LUID: 29190.792:0100:0148:info:vkd3d-proton:vkd3d_pipeline_library_disk_cache_merge: No write cache exists. No need to merge any disk caches.
  format support B8G8R8A8_UNORM          : hr=0x00000000 support1=0x03fcd3f3 support2=0x000002c0 SUPPORTED
  swapchain B8G8R8A8_UNORM          : hr=0x00000000 SUPPORTED
  format desc B8G8R8A8_UNORM          : hr=0x00000000 640x480 format=0x57 buffers=2 alpha=3 PASS
  readback B8G8R8A8_UNORM           red=191,22,42,255 green=21,192,42,255 blue=21,22,212,255 clear=5,5,51,255 : PASS
  present B8G8R8A8_UNORM          : hr=0x00000000 tearing=ALLOW_TEARING PASS
  format support B8G8R8A8_UNORM_SRGB     : hr=0x00000000 support1=0x03fcd3f1 support2=0x000002c0 SUPPORTED
  swapchain B8G8R8A8_UNORM_SRGB     : hr=0x00000000 SUPPORTED
  format desc B8G8R8A8_UNORM_SRGB     : hr=0x00000000 640x480 format=0x5b buffers=2 alpha=3 PASS
  readback B8G8R8A8_UNORM_SRGB      red=225,82,113,255 green=81,225,113,255 blue=81,82,235,255 clear=39,39,124,255 : PASS
  present B8G8R8A8_UNORM_SRGB   [mvk-info] Created 3 swapchain images with size (632, 446) and contents scale 1.0 in layer CAMetalLayer: WineMetalView (0x60000201b060) on screen Main Screen.
  format support R8G8B8A8_UNORM          : hr=0x00000000 support1=0x03fcd3f3 support2=0x000002c0 SUPPORTED
  swapchain R8G8B8A8_UNORM          : hr=0x00000000 SUPPORTED
  format desc R8G8B8A8_UNORM          : hr=0x00000000 640x480 format=0x1c buffers=2 alpha=3 PASS
  readback R8G8B8A8_UNORM           red=191,22,42,255 green=21,192,42,255 blue=21,22,212,255 clear=5,5,51,255 : PASS
  present R8G8B8A8_UNORM          : hr=0x00000000 tearing=ALLOW_TEARING [mvk-info] Created 3 swapchain images with size (632, 446) and contents scale 1.0 in layer CAMetalLayer: WineMetalView (0x6000021af780) on screen Main Screen.
  format support R10G10B10A2_UNORM       : hr=0x00000000 support1=0x03fcd3f3 support2=0x000002c0 SUPPORTED
  swapchain R10G10B10A2_UNORM       : hr=0x80070057 UNSUPPORTED
  swapchain format R10G10B10A2_UNORM: UNSUPPORTED (HRESULT classified); testing offscreen path
  offscreen R10G10B10A2_UNORM       : resource/RTV SUPPORTED; GPU render/readback UNSUPPORTED
  depth format support D24_UNORM_S8_UINT   : hr=0x00000000 support1=0x003110b0 SUPPORTED
  depth/stencil D24_UNORM_S8_UINT       : begin
  depth/stencil D24_UNORM_S8_UINT       : footprint=2560 rows=480 depth=0.992 stencil=UNSUPPORTED PASS (stencil clear/DSV exercised; readback unsupported)
  depth/stencil resource/DSV/barrier: PASS
  depth format support D32_FLOAT_S8X24_UINT: hr=0x00000000 support1=0x003110b0 SUPPORTED
  depth/stencil D32_FLOAT_S8X24_UINT    : begin
  depth/stencil D32_FLOAT_S8X24_UINT    : footprint=2560 rows=480 depth=1.000 stencil=UNSUPPORTED PASS (stencil clear/DSV exercised; readback unsupported)
  depth/stencil resource/DSV/barrier: PASS
  swapchain B8G8R8A8_UNORM          : hr=0x00000000 SUPPORTED
  format desc B8G8R8A8_UNORM          : hr=0x00000000 640x480 format=0x57 buffers=2 alpha=3 PASS
  GetColorSpace1: NOT EXPOSED (IDXGISwapChain3/4 provide Check/Set only)
  CheckColorSpaceSupport SDR P709        : hr=0x00000000 flags=0x1 SUPPORTED
  SetColorSpace1 SDR P709                : hr=0x00000000 PASS
  CheckColorSpaceSupport scRGB linear    : hr=0x00000000 flags=0x0 UNSUPPORTED
  SetColorSpace1 scRGB linear            : hr=0x80070057 UNSUPPORTED
  CheckColorSpaceSupport HDR10 PQ        : hr=0x00000000 flags=0x0 UNSUPPORTED
  SetColorSpace1 HDR10 PQ                : hr=0x80070057 UNSUPPORTED
  CheckColorSpaceSupport extended P2020  : hr=0x00000000 flags=0x0 UNSUPPORTED
  SetColorSpace1 extended P2020          : hr=0x80070057 UNSUPPORTED
  invalid HDR10 metadata(NULL): hr=0x80070057 PASS
  clear HDR metadata: hr=0x00000000 PASS
  valid HDR10 metadata update: hr=0x00000000 REPORTED
  unsupported BC1 swapchain: hr=0x80070057 PASS
  invalid alpha-mode swapchain: hr=0x80070057 PASS
  incompatible SDR-format/HDR10 color-space: hr=0x80070057 PASS
  unsupported BC1 render resource: hr=0x80070057 PASS
  invalid RTV description: PASS (void API returned)
  invalid DSV description: PASS (void API returned)
  invalid format support query: hr=0x80070057 support1=0x00000000 support2=0x00000000 PASS
  negative result: PASS (0 failures)
  tearing support: reported=1; accepted presentation flag policy=ALLOW_TEARING
DXGI-4 result: PASS (0 failures)
```

The configured runner reports HDR10/scRGB/P2020 presentation unsupported through DXGI on this run, despite the physical main display's capabilities. The TV was not used as evidence. The HDR setter's REPORTED HRESULT is not treated as HDR support; CheckColorSpaceSupport is authoritative. R10G10B10A2 is D3D12-format-supported and resource/RTV-creatable, but this native DXGI lane rejects its swapchain and the Metal conversion readback path is explicitly reported unsupported rather than synthesized. Stencil clear/DSV behavior is exercised; the backend copy footprint does not expose a stencil readback plane and that limitation is recorded explicitly.

The full two-run Wine/vkd3d/MoltenVK output is in dxgi-4-probe-run.txt and dxgi-4-probe-repeat.txt. DXGI-3 preservation output is in dxgi-4-phase3-gate.log. No broad gameplay-stability claim is made.
