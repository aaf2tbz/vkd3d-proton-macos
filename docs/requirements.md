# Requirements

## Supported validation host

The shipped M14 runtime was validated on an Apple M4 Mac running macOS with
Metal 4. The runtime DLLs are x86_64 PE binaries for Wine/Rosetta; the
MoltenVK dylib is universal (x86_64 + arm64).

## Required tools

| Tool | Requirement | Used for |
|---|---|---|
| Xcode + Command Line Tools | Xcode 27 beta 4 / CLT beta 5 in the validation workspace | Metal, metallib, Xcode universal MoltenVK build |
| llvm-mingw | `20260616-ucrt-macos-universal`, clang 22.1.8 | x86_64 Windows DLLs and PE probes |
| Meson + Ninja | current Homebrew versions | vkd3d-proton configuration/build |
| CMake | current Homebrew version | MoltenVK dependencies/build tooling |
| Python 3 | current supported version | deterministic packaging and helper scripts |
| zstd | current supported version | release archives |
| Git | current supported version | source and provenance management |
| DirectX Shader Compiler (`dxc`) | SM 6.x-capable build | HLSL/DXIL acceptance shaders |
| MetalSharp Wine 11.5 | installed launch runtime | Wine-side D3D12 probes |
| `gh` | authenticated with repository/release permissions | optional GitHub release publishing |

`make tools` runs the workspace validator. The validator expects the local
source clones and the exact validation paths configured in `scripts/env.sh`.
For a different machine, copy `scripts/env.sh` to a local override or edit
its paths; do not commit machine-specific paths.

## Source trees

The public repository intentionally does not contain the large source/build
trees. Place fresh clones at:

```text
sources/vkd3d-proton
sources/MoltenVK
sources/SPIRV-Cross
sources/MetalSharp
```

`make vkd3d` requires `sources/vkd3d-proton`; `make moltenvk` requires
`sources/MoltenVK` and its fetched dependencies. See
[Development.md](Development.md) for the exact commands.

## Runtime constraints

- Use a matched x86_64 `d3d12.dll` and `d3d12core.dll` pair.
- Keep `MoltenVK_icd.json` beside `libMoltenVK.dylib`; its library path is
  relative.
- Use the staged ICD explicitly through `VK_ICD_FILENAMES`.
- Do not use feature-level environment overrides as acceptance evidence.
- Do not replace the canonical MoltenVK artifact until the probes pass.
