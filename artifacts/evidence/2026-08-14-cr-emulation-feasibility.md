# 12_1 FEASIBILITY GATE: CONSERVATIVE RASTERIZATION TIER-1 EMULATION PIXEL-EXACT (2026-08-14)

Status: **feasibility PROVEN on GPU** (the 12_1 blocker). Pure-Metal prototype of
the D3D12 tier-1 (post-snap) conservative rasterization emulation is
pixel-exact against a CPU reference over 12 random seeds x 16 triangles.

## Emulation design (validated)

Pipeline-level emulation with NO hardware conservative rasterization:

1. **Vertex stage**: after the viewport transform, each vertex is pushed OUTWARD
   along its own angle-bisector by `dist = 2r / sin(θ/2)` where r = the pixel
   half-diagonal (0.707px) and θ = the local interior angle (clamped to a
   minimum sinHalf of 0.05). This expands every edge outward by 2r: r for the
   half-diagonal reach (the expanded triangle contains every pixel square that
   intersects the original) plus r margin covering the disk-dilation arc
   slivers at the vertices (sagitta = r(1-sin(θ/2)) ≤ r). The expanded
   triangle is a strict superset; the fragment stage decides final coverage.
   The original triangle's screen-space positions are passed to the fragment
   stage as flat varyings.
2. **Fragment stage**: D3D12 tier-1 POST-SNAP semantics: round the original
   vertices to the nearest pixel corner (integer pixel coords), then test the
   pixel CENTER against the snapped triangle's three edge functions (winding-
   aware, ε = 1e-4). Covered pixels write the app's output; uncovered pixels
   `discard_fragment()` (discard, NOT a black write - black overwrites earlier
   triangles' coverage and breaks union semantics).

## Bugs found and fixed during validation (all reproduced in pure Metal)

- Bisector direction must be OUTWARD (-normalize(e1+e2)); the inward version
  shrinks the triangle.
- The bisector is PER-VERTEX (this vertex's two adjacent edges) - using one
  vertex's bisector for all three destroys the expansion.
- Vertex y conversion NDC->framebuffer must use the pixel-index mirror
  (`H(1-y_ndc)/2 - 1`), not the continuous transform - off-by-one otherwise.
- Fragment coverage comparison must use the center-based y flip (fb pixel y
  <-> y-up pixel H-2-y).
- Reference and shader must use identical center convention (px+0.5, py+0.5).

## Evidence

scripts/probes/metal-cr-emulation-probe.mm renders 1 or 16 triangles at 64x64
RGBA8 with the emulation (VS expansion + FS post-snap test with discard),
reads back, and compares against a CPU reference implementing the identical
post-snap semantics:

```
ONE_TRI  : over=0 under=0  RESULT: CONSERVATIVE RASTER EMULATION PIXEL-EXACT
ALL 16   : over=0 under=0  (12 seeds: 1..12, 16 random triangles each)
```

Total: ~192 random triangles, ~4k pixels each, zero mismatches (over=0,
under=0 in every run).

## Residual notes (for the MVK implementation stage)

- Viewport: the expansion and the pixel conversion need the current viewport
  size at DRAW time (vkd3d uses dynamic viewports). MVK must pass the viewport
  size to the injected VS/FS via a reserved per-draw constants slot (e.g.,
  setVertexBytes at a slot not used by the app's shader).
- MSAA: the post-snap test is per-pixel (single-sample). MSAA conservative
  rasterization needs per-sample handling - document or implement later.
- Depth: the expanded triangle's interpolated depth differs from the original
  by < 1px of gradient (sub-pixel, documented deviation); the FS can override
  depth via [[depth(any)]] using the flat original triangle's depth plane.
- Attributes: interpolated at the pixel center from the expanded triangle
  (sub-pixel deviation, standard for emulated CR).
- Tier 3 (inner coverage, fullyCoveredFragmentShaderInputVariable) and
  degenerate-triangle semantics remain future work (tier 1 does not require
  them per the vkd3d tier mapping: degenerate=FALSE -> tier 1).
