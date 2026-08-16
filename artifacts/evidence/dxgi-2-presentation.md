# DXGI-2 windowed presentation evidence

- Date: 2026-08-16T20:51:03Z
- DXVK source: Gcenx/DXVK-macOS
- DXVK base commit: 8f1e28deed3ad30802f7e1bdff428ec14e6e7817
- DXVK bridge patch: /Volumes/AverySSD/VKD3D-Proton-MacOS/patches/dxvk-macos-d3d12-dxgi.patch
- DXVK bridge patch SHA-256: 366dbead003bec2e58fae03e138b82d01f3fece36af329a5c5976e20e2deeec1
- Wine runner: /tmp/run-probe.sh
- Host: 27.0

## Staged modules and hashes

```text
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
571e9f3b119aa0950993899ddf8df114be9277b347af8e1b12517eeb805d9aff  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
ac2b8674798bdbdd21ce1aa48daf1e2657813ecc878b80e2641bf0d2c3f2a43e  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
581c028a1e16bedad42671f2fb52fd8fc0d6b2b9b8f069852c8bcdc0e0509b52  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib: Mach-O universal binary with 2 architectures: [x86_64:Mach-O 64-bit dynamically linked shared library x86_64] [arm64]
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib (for architecture x86_64):	Mach-O 64-bit dynamically linked shared library x86_64
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib (for architecture arm64):	Mach-O 64-bit dynamically linked shared library arm64
38e0a7c3839390d524a3bb4b1165d13e96a2c3e771a14df2510c1ad5ab598bde  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/MoltenVK_icd.json
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/MoltenVK_icd.json: JSON data
578ff08cd0d8734619357541771a5abc9c3470ca300030219a971a9e9dbbe466  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/MoltenVK_icd.json
```

## Presentation acceptance

```text
=== DXGI-2 windowed presentation probe ===
selected adapter: vendor=0x106b device=0x1b000209 luid=00000000-000003f4
D3D12 adapter LUID: 00000000-000003f4 (MATCH)
tearing support: hr=0x00000000 supported=1
window/device/queue: PASS
=== CreateSwapChain flip-discard + Present ===
  create: hr=0x00000000 PASS
  Present(DXGI_PRESENT_TEST): hr=0x00000000 PASS
  readback center=0020ffff corner=330505ff: PASS
  frames: 1000 / 1000
  sync intervals exercised: 0 and 1; tearing flag on interval 0: ALLOW_TEARING
  GetFrameStatistics: hr=0x00000000 PASS/UNSUPPORTED
  mode result: PASS
=== CreateSwapChain flip-sequential + Present ===
  create: hr=0x00000000 PASS
  Present(DXGI_PRESENT_TEST): hr=0x00000000 PASS
  readback center=0020ffff corner=330505ff: PASS
  frames: 1000 / 1000
  sync intervals exercised: 0 and 1; tearing flag on interval 0: ALLOW_TEARING
  GetFrameStatistics: hr=0x00000000 PASS/UNSUPPORTED
  mode result: PASS
=== CreateSwapChainForHwnd flip-discard + Present1 ===
  create: hr=0x00000000 PASS
  Present1(DXGI_PRESENT_TEST): hr=0x00000000 PASS
  readback center=0020ffff corner=330505ff: PASS
  frames: 1000 / 1000
  sync intervals exercised: 0 and 1; tearing flag on interval 0: ALLOW_TEARING
  GetFrameStatistics: hr=0x00000000 PASS/UNSUPPORTED
  mode result: PASS
=== CreateSwapChainForHwnd flip-sequential + Present1 ===
  create: hr=0x00000000 PASS
  Present1(DXGI_PRESENT_TEST): hr=0x00000000 PASS
  readback center=0020ffff corner=330505ff: PASS
  frames: 1000 / 1000
  sync intervals exercised: 0 and 1; tearing flag on interval 0: ALLOW_TEARING
  GetFrameStatistics: hr=0x00000000 PASS/UNSUPPORTED
  mode result: PASS
negative result: PASS (0 failures)
DXGI-2 result: PASS
```

The full Wine/vkd3d/MoltenVK probe output is in dxgi-2-probe-run.txt and the repeatability run is in dxgi-2-probe-repeat.txt. DXVK logging was directed to the adjacent dxvk_* log when the provider emitted one.

## Existing regression summary

```text
cr_inner_probe.exe: RESULT: CR TIER 3 INNERCOVERAGE WORKS (D3D12 PATH)
feedback_probe.exe: RESULT: SAMPLER FEEDBACK MATCHES THE CPU REFERENCE EXACTLY
mesh_probe.exe: RESULT: MESH DISPATCH PIXEL-EXACT (D3D12 PATH)
corpus.exe: RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
corpus_gs.exe: RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
compute_matrix.exe: RESULT: CORE_1_0 COMPUTE MATRIX WORKS
```

This gate deliberately does not test resize, minimize, fullscreen, or broad gameplay stability; those belong to DXGI-3 and later.
