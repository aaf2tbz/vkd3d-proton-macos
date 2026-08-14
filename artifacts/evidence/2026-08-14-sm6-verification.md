# SM 6.0 verified + SM ladder analysis (2026-08-14)

## Shader model query (self-built stack, evidence m1-smquery.txt)
D3D12_FEATURE_DATA_SHADER_MODEL is IN-OUT (app requests max, driver returns min).
With HighestShaderModel input = 0x66 (6_6):
    -> mingw id 7 : S_OK, highest = 0x60  (D3D_SHADER_MODEL_6_0)
(My earlier probe zero-initialized the input -> min(0, x) = 0; corrected.)

## Why 6.0 and not higher (vkd3d SM ladder, device.c)
- 6.0 base: subgroupSize>=4 (MVK: 32), subgroup ops (MVK: 0x6ff = basic|vote|arithmetic|
  ballot|shuffle|shuffleRelative|clustered|quad|rotate|rotateClustered), stages
  (MVK: 0x32 = fragment|compute|tess-control), scalarBlockLayout (MVK: 1), shaderInt16 (1)
  -> PASSES (verified via standalone subgroup probe, scripts/probes/mvk-subgroup-probe.c)
- 6.2: requires denormBehaviorIndependence != VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE
  -> MVK sets denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE
     (MVKDevice.mm supportedProps12) -> BLOCKED. Metal has FP32 denorm control on Apple
     GPUs (MSL 2.3+); MVK work item: report 32_BIT_ONLY independence + denorm preserve/FTZ
     for FP32 (then SM 6.3 via SPIR-V 1.4 which MVK has, 6.5 unconditional).

## Impact
- 11_0 row "SM 5.1": satisfied (6.0 >= 5.1). Games querying SM 6.0+ now see a real value.
- 12_2 row needs SM 6.5: requires the MVK denorm-independence fix (small, contained).
