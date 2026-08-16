# Build VKD3D-Proton macOS

The [Build workflow](../.github/workflows/build.yml) is the best reference
for the portable repository checks. The root `Makefile` is the source of
truth for local build commands:

```bash
make help
```

This document is for developers who are comfortable with macOS toolchains,
cross-compilers, Meson/Ninja, Xcode, Wine, and GPU debugging. It is not a
beginner's introduction to those tools.

## Prerequisites

- Apple Silicon Mac with a Metal 3-capable GPU for the compatibility lane.
- macOS with Xcode and Command Line Tools installed.
- Xcode 16+ with the Metal toolchain. Xcode 16 is the reference macOS 14
  compatibility toolchain; newer Xcode versions may target macOS 14 too.
- `llvm-mingw-20260616-ucrt-macos-universal` with clang 22.1.8.
- Meson 1.x, Ninja, CMake, Python 3, Git, `zstd`, and `pkg-config`.
- DirectX Shader Compiler (`dxc`) for SM 6.x HLSL/DXIL probe shaders.
- A compatible Wine installation for Wine-side probes.
- `gh` authenticated with repository permissions for publishing releases.

Run the repository checks before building:

```bash
make docs-check
make tools
```

`make tools` checks the selected Xcode and source-clone paths. On another
machine, set `XCODE_DEVELOPER_DIR` and `WINE_BIN` locally; do not commit
machine-specific paths.

## Source setup

The public repository keeps large source and build trees out of Git. Create
fresh working trees at these paths:

```text
sources/vkd3d-proton/
sources/MoltenVK/
sources/SPIRV-Cross/
```

`make vkd3d` requires the vkd3d-proton tree. `make moltenvk` requires
MoltenVK and its fetched dependencies. Keep the SPIRV-Cross revision used by
MoltenVK aligned with `scripts/patch-spirv-cross.sh`.

## Setup the environment

```bash
source scripts/env.sh
```

The environment script configures the llvm-mingw path, Xcode developer
directory, generic Wine launcher, macOS deployment target, and workspace
output locations. Set
`WINE_BIN` to a specific Wine executable when it is not on `PATH`; set
`WINE_UNIX_LIB` when that Wine build needs an explicit Unix-library path. The
runtime target is x86_64 PE under Wine/Rosetta; MoltenVK is built universal
(x86_64 + arm64).

## Build vkd3d-proton

Build the matched D3D12 forwarder/implementation pair:

```bash
make vkd3d
```

This runs Meson with the generated
`artifacts/vkd3d-cross-x86_64.txt` cross file and Ninja. Outputs are:

```text
artifacts/build/vkd3d-proton/x86_64-windows/d3d12.dll
artifacts/build/vkd3d-proton/x86_64-windows/d3d12core.dll
```

Both files must be PE32+ x86-64 and must come from the same build. Never mix
DLLs from different lanes or build timestamps.

## Build DXVK macOS DXGI

Phase DXGI-1 uses the pinned DXVK macOS source at commit
`8f1e28deed3ad30802f7e1bdff428ec14e6e7817`:

```bash
git clone https://github.com/Gcenx/DXVK-macOS.git sources/dxvk-macos
git -C sources/dxvk-macos checkout 8f1e28deed3ad30802f7e1bdff428ec14e6e7817
make dxgi
make dxgi-probe
```

The provider is built as an x86_64 PE DLL at
`artifacts/build/dxvk-macos/x86_64-windows/dxgi.dll`. Stage it with the
matched D3D12 pair using `make stage`. The complete Phase 1 gate is:

```bash
make dxgi-test
```

That runs the adapter/factory probe ten times, checks stable DXGI/D3D12 LUID
identity against the MoltenVK GPU log, loads and checks the D3D12Core exports,
runs invalid-input tests, and reruns the existing six-probe regression suite.
DXGI-1 does not yet test windowed swapchains or gameplay presentation; those
are DXGI-2 and later.

## Build MoltenVK

Build the custom universal dylib candidate:

```bash
make moltenvk
```

The build fetches MoltenVK externals, reapplies the local SPIRV-Cross patches,
and writes a candidate to:

```text
artifacts/build/moltenvk-vkmt.build.new/
```

This directory is deliberately staged and not promoted automatically. Run
the validation gate first. Only after the probes pass should the candidate be
copied to `artifacts/build/moltenvk-vkmt/`.

Build both runtime components with:

```bash
make build
```

To build the compatibility profile with the downloaded Xcode 16 and a macOS
14 deployment target:

```bash
make metal3
```

To select the downloaded Xcode 16 explicitly:

```bash
XCODE16_DEVELOPER_DIR=/Users/averyfelts/Downloads/Xcode.app/Contents/Developer make metal3
```

Accept that Xcode installation's license first if `xcodebuild` reports
license error 69.

This verifies compilation against the current Xcode SDK while targeting
macOS 14. The runtime must still be exercised on a macOS 14 host to prove
Metal 3 behavior; a newer host will report and execute its own newer Metal
family.

## Build and stage probes

Compile the D3D12 feature-level probe and stage the canonical runtime:

```bash
make flprobe
make stage
```

`make flprobe` creates `artifacts/stage-dxr/flprobe.exe`. The acceptance
probes that use HLSL require DXIL blobs compiled by `dxc`; see
[validation.md](validation.md) for their expected layout and gate.

## Run validation

The Wine runner is host-specific. The established validation runner is
`/tmp/run-probe.sh`:

```bash
make ladder WINE_RUNNER=/tmp/run-probe.sh
```

The ladder must create devices for 12_2, 12_1, 12_0, 11_1, 11_0, and
1_0_CORE, with `max=12_2`. The full regression gate is documented in
[validation.md](validation.md). Do not accept a result without checking the
loaded module paths and hashes of the staged DLLs.

## Package and publish

After promotion and validation, create the deterministic runtime archive:

```bash
make package PACKAGE=./vkd3d-proton-macos.tar.zst
```

The archive contains the two D3D12 DLLs, universal MoltenVK, ICD manifest,
README, and SHA-256 checksums. Publish it as a GitHub release asset rather
than committing generated binaries:

```bash
gh release upload v1.0 ./vkd3d-proton-macos.tar.zst --clobber
```

See [release.md](release.md) for the runtime layout and installation
environment.

## Debugging

Useful diagnostics for the D3D12-on-Metal route include:

```bash
export WINEDEBUG=-all                    # reduce Wine noise
export VKD3D_DEBUG=trace                 # feature/layer diagnostics
export VKD3D_SHADER_DUMP_PATH=/tmp/dxil  # dump converted shaders
export MVK_CONFIG_SHADER_DUMP_DIR=/tmp/msl
export VK_LOADER_DEBUG=error             # Vulkan loader diagnostics
export MTL_DEBUG_LAYER=1                 # Metal API validation
export MTL_SHADER_VALIDATION=1           # Metal shader validation
```

For the validated Wine route, keep the ICD and dynamic-library paths
explicit:

```bash
export WINEDLLOVERRIDES="d3d12,d3d12core,dxgi=n,b"
export VK_ICD_FILENAMES="$WS/artifacts/stage-dxr/MoltenVK_icd.json"
export WINE_BIN="${WINE_BIN:-$(command -v wine)}"
export WINEPREFIX="$WS/artifacts/prefix"
export DYLD_LIBRARY_PATH="$WS/artifacts/stage-dxr${WINE_UNIX_LIB:+:$WINE_UNIX_LIB}"
export DYLD_FALLBACK_LIBRARY_PATH="$DYLD_LIBRARY_PATH"
```

Use `VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT=1` and `MVK_PRESENT_MODE=1` only
when reproducing the validated launch shape. Feature-level forcing is not
valid acceptance evidence.

## Logs and evidence

Wine-side probes print to stderr/stdout. Save the complete output, not only a
summary line:

```bash
cd artifacts/stage-dxr
/tmp/run-probe.sh flprobe.exe 2>&1 | tee /tmp/flprobe.log
```

Record candidate hashes, source commits, compiler versions, loaded module
paths, and probe output under `artifacts/evidence/`. The current feature
matrix is [features.md](features.md); the current regression rules are in
[validation.md](validation.md).

## Clean and contribute

Remove generated build configuration/output without touching source clones:

```bash
make clean
```

Before opening a change:

```bash
make test
git diff --check
```

Keep generated archives, local source trees, staged DLLs, and machine-specific
paths out of commits. Changes to a promoted runtime require fresh ladder and
regression evidence.
