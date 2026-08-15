# Slice 4: CR tier-3 InnerCoverage implementation (committed) + the D3D12 graphics-render debugging

## The critical launch-env discovery: dylib code signatures
The wine's winevulkan loads the Vulkan implementation via dlopen("libMoltenVK.1.dylib").
A plain `cp` of the freshly-built libMoltenVK into the override dir BROKE the code
signature ("code signature does not cover entire file") - dyld REJECTED the copy
and silently fell back to the runtime's shipped MoltenVK (1.4.2, no ray
query/CR/mesh) - the flprobe ladder collapsed (max=11_0, CR tier 0).
Fix: `codesign --force --sign - /tmp/mvkoverride/libMoltenVK.dylib` (+ the .1
copy) after every copy. The ladder (max=12_2, RaytracingTier=11,
MeshShaderTier=10, CR tier 3, all min levels create) is restored.

## The InnerCoverage implementation (MVK commit dfd3c70)
- SPIRV-Cross (workspace + the fetch-patched External): BuiltInFullyCoveredEXT
  emits as a plain `bool <name>` fragment argument (no Metal attribute; the
  MVK computes it). The build script (scripts/patch-spirv-cross.sh) re-applies
  the patch after fetchDependencies re-checks out the pinned SPIRV-Cross,
  BEFORE the XCFramework build.
- MVK fragment CR injection: the SV_InnerCoverage bool argument is removed from
  the fragment signature (Metal rejects attribute-less fragment inputs) and
  computed locally: all 4 pixel corners inside the ORIGINAL pre-snap triangle
  (the flat CR varyings) - the D3D12 tier-3 fully-covered semantics.
- The CR token-extraction fixes ("uint vid [[vertex_id]]" / the
  "[[position, invariant]]" / "[[stage_in]]" forms), the per-component position
  output override, the signature-separator cleanup.
- Verified: the pipeline creates (CR tier 3 + the InnerCoverage PS), the
  injected MSL is correct (the bool removed, the fully-covered assignment, the
  vertex expansion + varyings).

## The D3D12 graphics-render probe: draw-nothing (open)
The D3D12-level probe (scripts/probes/cr-inner/) renders a fullscreen triangle
with the CR mode + the InnerCoverage PS. The render-pass begins (RTV 64x64),
the draw executes (the MVK drawPrimitives), the vertex buffer is bound, the
vertex/fragment MSLs are correct - but no pixels land (the readback shows the
clear color). The Vulkan-level CR probe (vk-cr-probe) remains pixel-exact.
This is the FIRST D3D12-graphics-render probe in the session (all earlier
D3D12 probes were compute-only) - the vkd3d/MVK graphics-command-list path
needs further debugging (suspects: the render-pass store/load actions, the
vertex-buffer visibility, the vkd3d graphics state).
