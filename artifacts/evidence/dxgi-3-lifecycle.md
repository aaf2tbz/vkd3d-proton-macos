# DXGI-3 window lifecycle evidence

- Date: 2026-08-16T21:24:25Z
- DXGI source: Gcenx/DXVK-macOS, clean pinned base
- DXVK base commit: 8f1e28deed3ad30802f7e1bdff428ec14e6e7817
- DXVK bridge patch: /Volumes/AverySSD/VKD3D-Proton-MacOS/patches/dxvk-macos-d3d12-dxgi.patch
- DXVK bridge patch SHA-256: 366dbead003bec2e58fae03e138b82d01f3fece36af329a5c5976e20e2deeec1
- vkd3d-proton source revision: 5d24bc718560d6019fc2d74a41981be87bb2d9bd
- MoltenVK source revision: 13e3c967fd14e0dd8a00f456fd218380efbce73c
- Wine runner: /tmp/run-probe.sh
- Host: 27.0
- Compiler: clang version 22.1.8 (https://github.com/llvm/llvm-project.git ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)

## Staged modules and hashes

```text
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
1fbc9342605e9889731e42be9cb5916a6a271bff90f3bf4d31c1f4c7e4cbd030  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
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

## Lifecycle acceptance

```text
=== DXGI-3 window lifecycle probe (RGB triangle) ===
selected adapter: vendor=0x106b device=0x1b000209 luid=00000000-000003f4
D3D12 adapter LUID: 00000000-000003f4 (MATCH)
tearing support: hr=0x00000000 supported=1
window/device/queue: PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 640x480 format=0x57 buffers=2 PASS
  readback 640x480 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
=== resize matrix ===
  ResizeBuffers(800,600): hr=0x00000000 PASS
  desc: 800x600 format=0x57 buffers=2 PASS
  readback 800x600 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
  ResizeBuffers(320,240): hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  readback 320x240 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
  ResizeBuffers(1024,512): hr=0x00000000 PASS
  desc: 1024x512 format=0x57 buffers=2 PASS
  readback 1024x512 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
=== deterministic negative lifecycle tests ===
  invalid resize dimensions: hr=0x887a0001 PASS
  recover after invalid resize: hr=0x00000000 PASS
  desc: 640x480 format=0x57 buffers=2 PASS
  desc: 640x480 format=0x57 buffers=2 PASS
  invalid ResizeTarget(NULL): hr=0x887a0001 PASS
  invalid windowed fullscreen target: hr=0x887a0001 PASS
=== minimize/restore and occlusion ===
  minimized occlusion/test Present: hr=0x087a0001 PASS
  minimized ResizeBuffers(0,0): hr=0x00000000 SUPPORTED
  minimized dimensions: UNSUPPORTED (zero drawable)
  desc: 640x480 format=0x57 buffers=2 PASS
  readback 640x480 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
=== fullscreen and windowed fallback ===
  GetFullscreenState(initial): hr=0x00000000 windowed=1 PASS
  SetFullscreenState(TRUE): hr=0x00000000 SUPPORTED
  GetFullscreenState(after TRUE): hr=0x00000000 fullscreen=1 PASS
  ResizeTarget(lifecycle): hr=0x00000000 PASS
  SetFullscreenState(FALSE) fallback: hr=0x00000000 PASS
=== window destruction and recreation ===
  Present after window destruction: hr=0x00000000 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 640x480 format=0x57 buffers=2 PASS
  readback 640x480 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
=== 100 create/resize/destroy cycles ===
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  readback 384x288 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create/resize/destroy cycles: 25 / 100
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create/resize/destroy cycles: 50 / 100
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create/resize/destroy cycles: 75 / 100
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,408): hr=0x00000000 PASS
  desc: 768x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,360): hr=0x00000000 PASS
  desc: 640x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 384x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,408): hr=0x00000000 PASS
  desc: 704x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 448x336 format=0x57 buffers=2 PASS
  ResizeBuffers(768,360): hr=0x00000000 PASS
  desc: 768x360 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 512x240 format=0x57 buffers=2 PASS
  ResizeBuffers(640,408): hr=0x00000000 PASS
  desc: 640x408 format=0x57 buffers=2 PASS
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 320x288 format=0x57 buffers=2 PASS
  ResizeBuffers(704,360): hr=0x00000000 PASS
  desc: 704x360 format=0x57 buffers=2 PASS
  create/resize/destroy cycles: 100 / 100
  create lifecycle swapchain: hr=0x00000000 PASS
  desc: 640x480 format=0x57 buffers=2 PASS
  readback 640x480 rgb-red=2a16bf rgb-green=2ac015 rgb-blue=d41615 clear=330505: PASS
  Present after lifecycle event: hr=0x00000000 PASS
  shutdown: GPU idle
  shutdown: RTVs/resources
  shutdown: swapchain
  shutdown: queue/fence
  shutdown: device
  shutdown: adapter
  shutdown: factory
  shutdown: window
DXGI-3 result: PASS (0 failures)
```

The probe creates a real Win32 window, selects the Phase-1 adapter, renders the RGB triangle and clear color with GPU readback after each accepted lifecycle operation, and tracks PRESENT -> render-target -> copy-source -> PRESENT transitions. It exercises normal and zero/minimized resize, occlusion, fullscreen queries and fallback, destruction/recreation, invalid parameters, outstanding references, and 100 create/resize/destroy cycles. Shutdown is checked in GPU-idle, resources, swapchain, queue/fence, device, adapter, factory, window order.

DXGI-1 and DXGI-2 were rerun by the validator; their complete logs and the six-probe regression logs are adjacent to this document. This phase does not claim broad gameplay stability, resize/fullscreen support beyond the tested lane, or release readiness for later phases.

## Six-probe regression summary

```text
cr_inner_probe.exe: RESULT: CR TIER 3 INNERCOVERAGE WORKS (D3D12 PATH)
feedback_probe.exe: RESULT: SAMPLER FEEDBACK MATCHES THE CPU REFERENCE EXACTLY
mesh_probe.exe: RESULT: MESH DISPATCH PIXEL-EXACT (D3D12 PATH)
corpus.exe: RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
corpus_gs.exe: RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
compute_matrix.exe: RESULT: CORE_1_0 COMPUTE MATRIX WORKS
```
