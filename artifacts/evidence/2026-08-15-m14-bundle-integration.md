# M14: MetalSharp bundle integration — lanes + pins + tests

## Bundle repackaged
artifacts/metalsharp-graphics-dll-m12.tar.zst (121,024,510 bytes) via
tools/dmg/update-graphics-bundle.py against the published
metalsharp-graphics-dll.tar.zst, updating:
- Graphics/dll/vkd3d-proton/x86_64-windows/: d3d12.dll + d3d12core.dll
  (the DXR-1.1 / 12_2 / CORE_1_0 vkd3d fork build)
- Graphics/dll/moltenvk-vkmt/: libMoltenVK.dylib + MoltenVK_icd.json
  (the ray-query / mesh / depth-bounds MoltenVK fork build)
- the i386 vkd3d lane and every other entry preserved byte-for-byte.

## Hash pins (app/src-rust/src/installer.rs, production)
- VKD3D_PROTON_EXPECTED_HASHES:
  d3d12.dll 0fc399509bd8ae06f73621a1f55381b8ac79d62011c6b212ce20e5f42c982c3e
  d3d12core.dll 1659e641dec64eb4956fd96511a5ac9951d87ea3268e4294586fed6d94c26174
- MOLTENVK_VKMT_EXPECTED_HASHES:
  libMoltenVK.dylib 2e25de795e7df5e4bbd1715d9f39d54569ed5ac6ad31314c05204ab3ad35df43
(the test pins remain the fixture hashes).

## Verification
- installer tests: 33 passed (vkd3d_proton_runtime_current_requires_shipped_x86_64_hashes
  etc.)
- launcher tests: 100 passed (vkd3d_steam_launch, vkd3d_dry_run_includes_d3d12_dll,
  vkd3d_vkd3d_readiness_validates_vulkan_route)
- The PR commit is on the clean upstream tree (metalsharp/MetalSharp @ a097312 + 4cc04e3);
  the GitHub submission is the user's action (credentials).
