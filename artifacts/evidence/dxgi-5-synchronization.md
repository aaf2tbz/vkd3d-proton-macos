# DXGI-5 synchronization, pacing, and recovery evidence

- Date: 2026-08-16T23:18:45Z
- DXVK source commit: 8f1e28deed3ad30802f7e1bdff428ec14e6e7817
- DXVK bridge patch SHA-256: 366dbead003bec2e58fae03e138b82d01f3fece36af329a5c5976e20e2deeec1
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
943dc921530aeba8bc5add09f5a3c5fac7da50e90a84ca2f41f1b87ba532846e  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
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
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi_sync_probe.exe
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi_sync_probe.exe: PE32+ executable (console) x86-64, for MS Windows
ae0ca2f5f0a2228b7ea879acddc988bf702a0c466d1749872469fdcd00c7ffe4  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi_sync_probe.exe
```

## Long-run acceptance excerpt
```text
selected adapter: vendor=0x106b device=0x1b000209 luid=00000000-000003f9
D3D12 adapter LUID: 00000000-000003f9 (MATCH)
tearing support: hr=0x00000000 supported=1
frame latency object: SetMaximumFrameLatency(3) hr=0x00000000 SUPPORTED
window/device/queue/swapchain: PASS
frames-in-flight accepted mode=3
memory sample baseline     frame=0 working_set=113905664 peak=113905664
  queue Signal: hr=0x00000000 PASS
  cross-queue Signal/Wait: hr=0x00000000 PASS
  invalid fence event: hr=0x00000000 REPORTED
GetDeviceRemovedReason(after invalid operation): hr=0x00000000 S_OK
GetDeviceRemovedReason(after timeout path): hr=0x00000000 S_OK
  out-of-order fence signal: UNSUPPORTED (unsafe operation not issued)
  fence semantics: PASS
  release before GPU completion: UNSUPPORTED (unsafe negative not issued)
  DXGI_PRESENT_TEST: hr=0x00000000 PASS
GetDeviceRemovedReason(after occlusion): hr=0x00000000 S_OK
  IDXGIDevice3::Trim: hr=0x80004002 UNSUPPORTED
memory sample stress       frame=0 working_set=117284864 peak=117284864
  frame run interval=0 frames=8: PASS
  frame run interval=1 frames=8: PASS
memory sample stress       frame=10000 working_set=122593280 peak=122593280
memory sample stress       frame=20000 working_set=122675200 peak=122675200
memory sample stress       frame=30000 working_set=122703872 peak=122703872
memory sample stress       frame=40000 working_set=122740736 peak=122740736
memory sample stress       frame=50000 working_set=122769408 peak=122769408
memory sample stress       frame=60000 working_set=122753024 peak=122769408
memory sample stress       frame=70000 working_set=122785792 peak=122785792
memory sample stress       frame=80000 working_set=122830848 peak=122830848
memory sample stress       frame=90000 working_set=122830848 peak=122830848
memory sample stress       frame=100000 working_set=122843136 peak=122843136
  frame run interval=0 frames=100000: PASS
memory sample final        frame=100016 working_set=122843136 peak=122843136
GetDeviceRemovedReason(normal): hr=0x00000000 PASS
memory bounded: baseline/peak/final samples recorded peak=122843136: PASS
  shutdown: GPU idle
  shutdown: RTVs/resources
  shutdown: swapchain
  shutdown: queue/fence
  shutdown: device
  shutdown: adapter
  shutdown: factory
  shutdown: window
DXGI-5 result: PASS (0 failures)
```

The waitable object, cross-queue path, Trim, and device-loss recovery are reported from actual HRESULTs. Unsupported backend behavior is not promoted to a pass. The run is synthetic synchronization evidence only and makes no broad gameplay-stability claim.
