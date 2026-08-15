# dxc tooling (follow-up prerequisite)

- dxc: microsoft/DirectXShaderCompiler v1.9.2607 (dxc_2026_07_29.zip), the
  Windows x64 binary run under the MetalSharp wine runtime (no macOS release
  exists; DXIL output is platform-independent).
- Wrapper: scripts/dxc.sh (WINEPREFIX=artifacts/prefix, dxc under wine).
- Verified compiles:
  - cs_6_0 wave64.hlsl: WaveGetLaneCount + WavePrefixSum(1ull) + int64 math
    -> /tmp/cs60b.dxil (3096 B)
  - ms_6_5 mesh-triangle.hlsl: [numthreads(64,1,1)] mesh shader, 3 verts / 1
    tri, SetMeshOutputCounts -> /tmp/ms65.dxil (3164 B)
- The wine d3dcompiler_47 remains SM 5.x only; all SM 6.x shaders for the
  probes now compile through scripts/dxc.sh.
