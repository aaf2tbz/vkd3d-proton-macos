# VKD3D-Proton macOS — Final State

**Status:** shipped and publicly released as [`v1.0`](https://github.com/aaf2tbz/vkd3d-proton-macos/releases/tag/v1.0).

This is the consolidated final-state record for the Apple Silicon D3D12
runtime. It replaces the numbered planning/evidence documents that were used
during development.

## Runtime route

```text
D3D12 application
        │
        ▼
MetalSharp Wine 11.5 / Rosetta x86_64
        │
        ▼
vkd3d-proton: d3d12.dll + d3d12core.dll
        │
        ▼
Vulkan / winevulkan
        │
        ▼
custom MoltenVK: universal x86_64 + arm64
        │
        ▼
Apple Metal 4 / Apple M4
```

The runtime is intentionally x86_64 on the Wine/D3D12 side and universal on
the MoltenVK side. Keep the ICD manifest beside the dylib and pin it with
`VK_ICD_FILENAMES`.

## Released package

Download [`vkd3d-proton-macos.tar.zst`](https://github.com/aaf2tbz/vkd3d-proton-macos/releases/tag/v1.0).
The archive contains:

```text
vkd3d-proton-macos/
├── d3d12.dll
├── d3d12core.dll
├── libMoltenVK.dylib
├── MoltenVK_icd.json
├── README.md
└── SHA256SUMS
```

Release artifact hashes:

| Artifact | SHA-256 |
|---|---|
| `d3d12.dll` | `ac2b8674798bdbdd21ce1aa48daf1e2657813ecc878b80e2641bf0d2c3f2a43e` |
| `d3d12core.dll` | `581c028a1e16bedad42671f2fb52fd8fc0d6b2b9b8f069852c8bcdc0e0509b52` |
| `libMoltenVK.dylib` | `5f7fb30c669e95a2a041015af799694ee03ce924f6555679e5e6789d4d171fea` |
| `MoltenVK_icd.json` | `578ff08cd0d8734619357541771a5abc9c3470ca300030219a971a9e9dbbe466` |

`libMoltenVK.dylib` is a universal x86_64/arm64 Mach-O with a valid adhoc
signature for the isolated runtime override path.

## Feature-level ladder

The final `flprobe.exe` run on Apple M4 returned `S_OK` and `dev=CREATED` for
every requested minimum level:

| Minimum feature level | Result |
|---|---|
| 12_2 | created |
| 12_1 | created |
| 12_0 | created |
| 11_1 | created |
| 11_0 | created |
| CORE_1_0 | created |

`CheckFeatureSupport(FEATURE_LEVELS)` reports **12_2** as the maximum. The
probe handles the known mingw-w64 renumbering of several feature-query IDs by
trying the official Microsoft IDs and then the ABI-compatible mingw IDs.
That fallback is not feature forcing.

## Validated capability matrix

| Capability | Final reported result | Acceptance evidence |
|---|---:|---|
| Shader Model | 6.5 | feature ladder |
| DXR / ray tracing | tier 1.1 (`RaytracingTier=11`) | inline ray-query path and ladder |
| Variable-rate shading | tier 2 | feature ladder |
| Mesh shaders | tier 1 (`MeshShaderTier=10`) | pixel-exact mesh dispatch |
| Sampler feedback | tier 0_9 (`SamplerFeedbackTier=90`) | exact CPU-reference readback |
| Tiled resources | tier 4 | sparse execution evidence and ladder |
| Conservative rasterization | tier 3 | InnerCoverage/CR probe |
| Rasterizer-ordered views | supported | ordering probe |
| Output-merger logical operations | supported | pixel-exact logic-op matrix |
| Depth bounds | supported | feature ladder |
| Copy-queue timestamps | supported | feature ladder |
| Fully typed casting | supported | feature ladder |
| Barycentrics | supported | feature ladder |

Capability reporting is distinguished from full API-surface claims. In
particular, the full TraceRay/RTPSO path and the optional task/object mesh
amplification variant remain separate follow-up work; the shipped acceptance
claims are the inline ray-query and mesh-only paths listed above.

## Regression gate

The final companion gate passed:

- `cr_inner_probe.exe` — CR tier-3 InnerCoverage matches the reference;
- `feedback_probe.exe` — sampler feedback matches the CPU reference exactly;
- `mesh_probe.exe` — mesh dispatch is pixel-exact;
- `corpus.exe` — CORE_1_0 compute corpus works;
- `corpus_gs.exe` — geometry-shader corpus works; and
- `compute_matrix.exe` — CORE_1_0 compute matrix works.

## Delivered implementation

- vkd3d-proton MS/AS graphics pipeline state layout and extraction were
  restored and kept ABI-consistent with the generated D3D12 headers.
- MoltenVK gained the mesh-stage plumbing, mesh draw dispatch path, and the
  required null guard in reserved vertex-attribute setup.
- SPIRV-Cross gained sampler-feedback 64-bit image-atomic lowering and the
  storage-image access fix needed for descriptor-array/function-parameter
  paths.
- Conservative rasterization, logic-op emulation, ROV ordering, sparse
  resources, ray-query, CORE_1_0, mesh, and sampler-feedback probes provide
  deterministic GPU/readback evidence for the promoted capabilities.
- The release process produces a small runtime archive rather than placing
  generated binary bundles in Git history.

## Reproduce the build

Install the tools in [requirements.md](requirements.md), place fresh source
trees under `sources/`, and use the root Makefile:

```bash
make docs-check
make tools
make vkd3d
make moltenvk
make flprobe
make stage
make ladder WINE_RUNNER=/tmp/run-probe.sh
make package PACKAGE=./vkd3d-proton-macos.tar.zst
```

The complete developer workflow, debugging variables, and release procedure
are in [Development.md](Development.md). Validation rules are in
[validation.md](validation.md), and runtime installation is in
[release.md](release.md).

## Repository policies

- [Contributing](../CONTRIBUTING.md)
- [Security](../SECURITY.md)
- [Code of Conduct](../CODE_OF_CONDUCT.md)

## Final provenance

- Public repository: <https://github.com/aaf2tbz/vkd3d-proton-macos>
- Release tag: `v1.0`
- Final documentation/policy state is checked by `make docs-check`.
- Generated source clones, build directories, and release archives remain
  outside Git; the release asset is the distribution artifact.
