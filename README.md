<div align="center">

<h1>VKD3D-Proton macOS</h1>

<p>
  Reproducible D3D12-on-Metal runtime for Apple Silicon.
</p>

<p>
  <a href="https://github.com/aaf2tbz/vkd3d-proton-macos/releases/tag/m14"><img src="https://img.shields.io/github/release/aaf2tbz/vkd3d-proton-macos/m14.svg?style=for-the-badge&label=Release" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge" alt="MIT License"></a>
  <a href="https://github.com/aaf2tbz/vkd3d-proton-macos/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/aaf2tbz/vkd3d-proton-macos/build.yml?branch=main&style=for-the-badge&label=Build" alt="Build"></a>
</p>

<pre>D3D12 application → vkd3d-proton → Vulkan → custom MoltenVK → Metal</pre>

<p>
  The public <a href="https://github.com/aaf2tbz/vkd3d-proton-macos/releases/tag/m14">M14 release</a>
  contains the tested x86_64 Wine D3D12 runtime pair and universal MoltenVK runtime.
</p>

</div>

## Capabilities

The Apple validation lane reports and exercises:

- D3D12 feature levels **12_2, 12_1, 12_0, 11_1, 11_0**, and **CORE_1_0**
- Shader Model **6.5**
- DXR **1.1** / ray-tracing tier 1.1
- Variable-rate shading tier 2
- Mesh-shader tier 1 and pixel-exact mesh dispatch
- Sampler-feedback tier 0_9 with 64-bit image-atomic lowering
- Tiled resources tier 4, conservative rasterization tier 3, ROVs, depth
  bounds, barycentrics, typed-format casting, copy-queue timestamps, and
  output-merger logical operations

All six feature-level ladder rungs create successfully. The companion
regression gate covers CR InnerCoverage, sampler feedback, mesh dispatch,
CORE_1_0 corpus, geometry-shader corpus, and the compute matrix.

## Quick start

```bash
git clone https://github.com/aaf2tbz/vkd3d-proton-macos.git
cd vkd3d-proton-macos
make help
make docs-check
```

For a complete local build, clone the required source trees into `sources/`,
install the toolchain described in [the requirements guide](docs/requirements.md),
then run:

```bash
make tools       # verify the host and external tools
make vkd3d      # build d3d12.dll + d3d12core.dll
make moltenvk   # build the universal libMoltenVK.dylib
make flprobe    # build the D3D12 feature-level probe
```

The full target map is documented in [docs/build.md](docs/build.md). Do not
mix `d3d12.dll` and `d3d12core.dll` from different builds.

## Repository layout

```text
.
├── Makefile                         # build, probe, package, and doc targets
├── ROADMAP.md                       # milestone history and technical roadmap
├── docs/                            # current guides plus dated evidence
├── scripts/                         # environment, build, probe, and recovery tools
├── sources/                         # local source clones (intentionally ignored)
├── toolchain/                       # local llvm-mingw/toolchain assets
└── artifacts/                       # local builds, staging, and evidence
```

## Documentation

Start at [docs/README.md](docs/README.md), then see:

- [Requirements](docs/requirements.md)
- [Build and Make targets](docs/build.md)
- [Feature-level matrix](docs/features.md)
- [Validation and regression](docs/validation.md)
- [Release and packaging](docs/release.md)
- [Dated evidence](docs/01-feature-level-evidence.md)

## License

Workspace code and documentation are MIT licensed. The vkd3d-proton,
MoltenVK, SPIRV-Cross, Wine, and other vendored/upstream components retain
their own licenses; consult their source trees and license files.
