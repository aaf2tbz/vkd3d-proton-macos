# DXGI-1 adapter identity evidence

- Date: 2026-08-16T20:16:35Z
- DXVK source: Gcenx/DXVK-macOS
- DXVK commit: 8f1e28deed3ad30802f7e1bdff428ec14e6e7817
- Wine runner: /tmp/run-probe.sh
- Host: 27.0

## Staged artifacts

```text
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll:      PE32+ executable (DLL) (GUI) x86-64, for MS Windows
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll:     PE32+ executable (DLL) (GUI) x86-64, for MS Windows
/Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
6670be883365d4bbe60a503e9c33ee45fd3c13e6c621b7b42554d1b7c6361153  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/dxgi.dll
ac2b8674798bdbdd21ce1aa48daf1e2657813ecc878b80e2641bf0d2c3f2a43e  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12.dll
581c028a1e16bedad42671f2fb52fd8fc0d6b2b9b8f069852c8bcdc0e0509b52  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/d3d12core.dll
38e0a7c3839390d524a3bb4b1165d13e96a2c3e771a14df2510c1ad5ab598bde  /Volumes/AverySSD/VKD3D-Proton-MacOS/artifacts/stage-dxr/libMoltenVK.dylib
```

## Identity result

```text
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_stencil_export v1
warn:  CreateDXGIFactory2: Ignoring flags
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_stencil_export v1
21591.874:0020:0024:info:vkd3d-proton:vkd3d_instance_apply_application_workarounds: Program name: "dxgi_probe.exe" (hash: 2b9f368979fa842c)
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_stencil_export v1
=== DXGI-1 module identity ===
  module dxgi.dll    : Z:\Volumes\AverySSD\VKD3D-Proton-MacOS\artifacts\stage-dxr\dxgi.dll
  module d3d12.dll   : Z:\Volumes\AverySSD\VKD3D-Proton-MacOS\artifacts\stage-dxr\d3d12.dll
  module d3d12core.dll: NOT LOADED
  CreateDXGIFactory : 0x00000000 PASS
  CreateDXGIFactory1: 0x00000000 PASS
  CreateDXGIFactory2: 0x00000000 PASS
  adapter 0: desc=0x00000000 vendor=0x106b device=0x1b000209 flags=0x0
    name: Apple GPU
  adapter LUID      : 00000000-000003f4
    outputs: 2
  total outputs: 2
  selected hardware adapter: PASS
    name: Apple GPU
  selected LUID     : 00000000-000003f4
=== D3D12 adapter identity ===
  D3D12CreateDevice(adapter): 0x00000000 PASS
  D3D12 LUID        : 00000000-000003f4
  DXGI/D3D12 LUID match: PASS
  module d3d12core.dll: Z:\Volumes\AverySSD\VKD3D-Proton-MacOS\artifacts\stage-dxr\d3d12core.dll
  LoadLibrary(d3d12core): PASS
  D3D12GetInterface export: PASS
  D3D12SDKVersion export: PASS
DXGI-1 result: PASS (0 failures)
	model: Apple GPU
	vendorID: 0x106b
	deviceID: 0x1b000209
	model: Apple GPU
	vendorID: 0x106b
	deviceID: 0x1b000209
	model: Apple GPU
	vendorID: 0x106b
	deviceID: 0x1b000209
	model: Apple GPU
	vendorID: 0x106b
	deviceID: 0x1b000209
	model: Apple GPU
	vendorID: 0x106b
	deviceID: 0x1b000209
	model: Apple GPU
	vendorID: 0x106b
	deviceID: 0x1b000209
[mvk-info] Created VkDevice to run on GPU Apple GPU with the following 35 Vulkan extensions enabled:
	model: Apple GPU
```

Ten repeated runs produced the same selected adapter LUID:

```text
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
  selected LUID     : 00000000-000003f4
```

The full first-run and negative-test logs are stored beside this record.

## Regression summary

```text
cr_inner_probe.exe: RESULT: CR TIER 3 INNERCOVERAGE WORKS (D3D12 PATH)
feedback_probe.exe: RESULT: SAMPLER FEEDBACK MATCHES THE CPU REFERENCE EXACTLY
mesh_probe.exe: RESULT: MESH DISPATCH PIXEL-EXACT (D3D12 PATH)
corpus.exe: RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
corpus_gs.exe: RESULT: CORE_1_0 SM 6.0 CORPUS WORKS
compute_matrix.exe: RESULT: CORE_1_0 COMPUTE MATRIX WORKS
```
