# logic-op implementation status (2026-08-14)

## DONE (committed to the moltenvk-macos fork + workspace repo)
1. features.logicOp = 1 on Apple GPUs (framebuffer-fetch emulation, not private API)
   - MVKPhysicalDeviceMetalFeatures.supportsLogicOpEmulation (vendor==Apple)
2. MVKGraphicsPipeline::initLogicOpEmulation(): detects logicOpEnable BEFORE shader
   conversion; blending disabled for affected attachments (VUID blendEnable=FALSE)
3. MSL injection (MVKShaderLibraryCache::getShaderLibrary -> mvkInjectLogicOpEmulation):
   - typed framebuffer-fetch input `float4/half4 _mvkLogicOpDst [[color(N)]]`
     (type matched to the attachment output member)
   - static apply helper: 16 VkLogicOp expressions over uchar4, 8-bit normalized
   - return wrapping for every `return X;`
   - handles SPIRV-Cross struct outputs (main0_out) and scalar half4/float4 forms
4. VERIFIED: injected MSL compiles with xcrun metal; pipeline creation succeeds;
   feature bit reported; Metal-level framebuffer-fetch XOR proven pixel-exact
   (metal-logicop-probe.mm).

## OPEN (probe-side, affects stock MVK equally)
The Vulkan functional probe (vk-logicop-probe.c, render-pass based) crashes with
MTLCommandBuffer "Invalid Resource (code 9)" when a TRANSFER COPY and a RENDER PASS
share a command buffer (any order, with or without barriers). Isolations:
- transfer-only round trip (buffer->image->buffer): WORKS
- render-pass-only (clear, no copy): WORKS
- copy + render pass (same or separate command buffers): CRASH
- Also on the STOCK shipped MoltenVK (50e41de2) -> pre-existing MVK behavior with
  this exact usage pattern, NOT caused by the logic-op changes.
This is worth a dedicated MVK investigation (potential real bug affecting
transfer+render interleaving; note the D3D12 route's readback patterns).
The Metal-level proof + injection + compile verification stand as the logic-op evidence
until the probe is fixed.

## Next
- Fix the probe's transfer+renderpass interaction (or use a framebuffer attached
  image read back via a second render pass / blit-only path)
- Then: full D3D12-route verification (vkd3d logic-op pipeline + game)
