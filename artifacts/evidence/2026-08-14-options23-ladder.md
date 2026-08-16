# 12_2 ladder: OPTIONS2/3 empirical status (2026-08-14, evidence options23-run1.txt)

Self-built stack under Wine 11.5, flprobe extended with OPTIONS2/3 (mingw ids 18/21):
  OPTIONS2: DepthBoundsTestSupported=0  ProgrammableSamplePositionsTier=0
  OPTIONS3: CopyQueueTimestampQueries=1  CastingFullyTyped=1  Barycentrics=1

12_2 rung condition status:
  max_shader_model >= 6.5        ✅ (SM 6.5 achieved)
  TIR                            ✅ (1)
  WaveOps / Int64ShaderOps       ✅ (SM>=6.0 / shaderInt64)
  DepthBoundsTestSupported       ❌ features.depthBounds=0 (MVK) - needs MVK emulation
  CopyQueueTimestampQueries      ✅ (MVK timestampValidBits=64 on all families)
  CastingFullyTypedFormat        ✅ (vkd3d hardcodes TRUE)
  RB tier >= 3                   ✅ (hardcoded 3)
  CR tier >= 3                   ❌ 12_1
  Tiled tier >= 3                ❌ 12_0
  RaytracingTier >= 1.1          ❌ DXR
  VRS tier >= 2                  ❌ VRS and sampler feedback
  MeshShaderTier >= 1            ❌ mesh
  SamplerFeedbackTier >= 0.9     ❌ SM 6.5
