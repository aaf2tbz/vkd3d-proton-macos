# SM 6.2+ blocker: Metal FP32 denormals are FTZ-only on Apple M4 (2026-08-14)

Probe: scripts/probes/metal-denorm-probe.mm (runtime-compiled compute kernel,
denormal FLT_MIN/2 = 1.4e-45, `o[0] = i[0] * 2.0f`)

## Results
- fastMath=YES (MVK default): denorm*2 = 0            -> FLUSHED TO ZERO
- fastMath=NO               : denorm*2 = 0            -> FLUSHED TO ZERO
- Offline compiler: `[[denorm_mode]]` attribute = unknown (ignored); 
  `-fdenormal-fp-math=on` = invalid; only preserve-sign/positive-zero accepted
  (not usable by MVK's runtime compilation path anyway: MTLCompileOptions has
  no denorm control).

## Consequence for the vkd3d SM ladder (device.c)
SM 6.2 rung requires `denormBehaviorIndependence != NONE` AND
`shaderDenormPreserveFloat32 && shaderDenormFlushToZeroFloat32` (non-NV).
- shaderDenormFlushToZeroFloat32: TRUE (default FTZ) 
- shaderDenormPreserveFloat32: FALSE (cannot preserve - hardware baseline)
-> SM ladder stops at 6.0. SM 6.5 (required by the 12_2 rung) unreachable
   without one of:
   a) Apple adds denorm-preserve capability (future OS/hardware) - out of our hands;
   b) vkd3d fork relaxation: allow FTZ-only for the SM 6.2 rung on this stack,
      documented as a portability deviation (D3D12 hardware today varies;
      denorm-control shaders would observe FTZ results - correctness caveat
      must be recorded per-game). THIS IS A ROADMAP DECISION (M11/4D.1).

## Notes
- Metal 4 / MSL 4.0 on Apple M4: no denorm-mode shader attribute, no compile
  option. MVK cannot expose preserve semantics honestly today.
- This does NOT affect 11_0/11_1/12_0/12_1 rungs (SM 6.0 >= their requirements).
