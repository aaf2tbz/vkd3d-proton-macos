# logic-op feasibility: MSL framebuffer fetch logic ops PROVEN on Apple GPU (2026-08-14)

Probe: scripts/probes/metal-logicop-probe.mm
- Fragment shader declares framebuffer-fetch input: half4 dst [[color(0)]]
- Fixed-function blending DISABLED (blendingEnabled=NO)
- Shader computes src ^ dst per 8-bit channel (uchar4), returns result
- Seeded dst R=0xAA; src.R=trunc(0.1*255)=25 (0x19)
- Readback: R=0xB3 = 0xAA^0x19  (G/B/A = src^0 = src) -> PIXEL-EXACT
RESULT: FRAMEBUFFER FETCH + LOGIC OP WORKS

Implications for logic-op (docs/04 design):
- [[color(n)]] framebuffer fetch is available on Apple GPU with blending off ->
  programmable blending path confirmed
- Metal's color writeMask still applies at the ROP write stage with blending
  off, so masked channels keep the destination automatically
- Open work: per-format exactness (8/16/32-bit int paths, RGB10A2 packing,
  SNORM, SRGB-encoded byte semantics), MSAA per-sample fetch validation,
  MVK MSL injection mechanism (entry signature + return wrapping), feature-bit
  gating (logicOp only when emulation active)
