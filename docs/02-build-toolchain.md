# Build Toolchain — Inventory & Recipes

All paths relative to the workspace root `/Volumes/AverySSD/VKD3D-Proton-MacOS` unless absolute.

## 1. Toolchain inventory (verified 2026-08-14)

| Tool | Source | Location | Version |
|---|---|---|---|
| **llvm-mingw** | official release (downloaded into workspace) | `toolchain/llvm-mingw-20260616-ucrt-macos-universal/` | clang 22.1.8 — **exact compiler of the shipped custom d3d12core.dll** (`ca7933e47d…`) |
| **ninja** | Homebrew | `/opt/homebrew/bin/ninja` | (symlinked into `toolchain/`) |
| **meson** | Homebrew | `/opt/homebrew/bin/meson` | (symlinked into `toolchain/`) |
| **cmake** | Homebrew | `/opt/homebrew/bin/cmake` | (symlinked into `toolchain/`) |
| **Xcode 27 beta 4** | `/Users/averyfelts/Downloads/Xcode-beta.app` | `toolchain/Xcode-beta.app` (symlink) | Xcode 27.0 (27A5228h); `xcrun metal` = 32023.921 |
| **CLT beta 5** | `/Library/Developer/CommandLineTools` | system | Apple clang 21.0.0 (clang-2100.3.30.1) |
| zstd, pkg-config, python3 | Homebrew | system | — |
| MetalSharp Wine 11.5 | installed (internal) | `/Users/averyfelts/.metalsharp/runtime/wine` | launch/probe runtime; never modified until M14 |

`scripts/env.sh` exports: `LLVM_MINGW`, `PATH` (llvm-mingw bin first for PE work), `DEVELOPER_DIR` (Xcode beta), `MS_WINE` (wine runtime), and workspace vars.

## 2. Sources (fresh clones + custom baseline)

```
sources/
├── vkd3d-proton/               HansKristian-Work/vkd3d-proton  (--recursive: SPIRV-Headers, Vulkan-Headers, dxil-spirv via meson wraps/submodules)
├── MoltenVK/                   KhronosGroup/MoltenVK           (fresh upstream — BUILD BASE for the custom MoltenVK; externals via ./fetchDependencies)
├── SPIRV-Cross/                KhronosGroup/SPIRV-Cross        (standalone pinning target, alongside MoltenVK)
├── MetalSharp/                 metalsharp/MetalSharp            (clean upstream tree for the PR)
└── moltenvk-vkmt-custom-bundle/                                 OUR custom MoltenVK, extracted from the bundle:
    └── Graphics/dll/moltenvk-vkmt/{libMoltenVK.dylib, MoltenVK_icd.json}
        # MoltenVK 1.4.2 / Vulkan 1.4.357 · sha256 50e41de2… · embedded SPIRV-Cross
        # This is the canonical CUSTOM artifact: baseline for all probes and the
        # reference the rebuilt custom source must surpass before replacement.
```

Working copy of the canonical dylib lives at `artifacts/build/moltenvk-vkmt/` (probes and staging point here).

Discipline: SPIRV-Cross edits happen in `sources/SPIRV-Cross`, are pinned into `sources/MoltenVK/External/SPIRV-Cross`, and **MoltenVK's nested artifact is rebuilt** after any cross edit (stale-external pitfall).

## 3. vkd3d-proton PE build (x86_64-windows, llvm-mingw + ninja + meson)

```bash
source scripts/env.sh
cd sources/vkd3d-proton

# cross file for the Wine PE target
cat > ../vkd3d-cross-x86_64.txt <<'EOF'
[binaries]
c = 'x86_64-w64-mingw32-clang'
cpp = 'x86_64-w64-mingw32-clang++'
ar = 'llvm-ar'
strip = 'llvm-strip'
windres = 'llvm-windres'
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
[properties]
needs_exe_wrapper = true
EOF

# mirror of the custom build's split layout:
#   build-vkmt-win64-filtered  →  d3d12.dll (forwarder) + d3d12core.dll (impl)
meson setup --cross-file ../vkd3d-cross-x86_64.txt \
    --buildtype release --strip \
    -Denable_d3d12core=true \
    ../../artifacts/build/vkd3d-proton-build . 
ninja -C ../../artifacts/build/vkd3d-proton-build
# outputs land in artifacts/build/vkd3d-proton/x86_64-windows/{d3d12.dll,d3d12core.dll}
```

- `file` must report `PE32+ … x86-64` for **both** DLLs; hashes recorded in the evidence doc.
- The VKMT custom patches (Section 3 of ROADMAP.md) are re-derived as commits on a workspace branch before our feature work.

## 4. MoltenVK build (universal dylib)

```bash
source scripts/env.sh
cd sources/MoltenVK
# update externals once, then pin
./fetchDependencies --skip-vulkan-tools 2>/dev/null || true
# universal release build via CMake/Xcode; package dir is the artifact
./PackageReleaseScript.sh -cmk  # or: cmake + xcodebuild per repo README
cp -R Package/Release/MoltenVK/dynamic/dylib/macOS/{libMoltenVK.dylib,MoltenVK_icd.json} \
      ../../artifacts/build/moltenvk-vkmt/
```

Requirements:
- Universal (x86_64 + arm64) — the runtime is Rosetta x86_64; `file` must show both slices.
- `MoltenVK_icd.json` keeps `"library_path": "./libMoltenVK.dylib"` (relative, same dir).
- **Baseline rule:** until the rebuilt custom source demonstrably surpasses it, the
  canonical artifact is the bundle's custom dylib (`artifacts/build/moltenvk-vkmt/`,
  sha `50e41de2…`). A fresh build must never silently replace it — stage build
  outputs separately and promote only with probe evidence.
- Every new Vulkan feature ships disabled until its functional probe row is green.

## 5. MSL / metallib (Xcode 27b4 + CLT beta 5)

```bash
source scripts/env.sh                       # sets DEVELOPER_DIR to Xcode-beta
xcrun -sdk macosx metal --version           # 32023.921
xcrun -sdk macosx metal -c shader.metal -o shader.air
xcrun -sdk macosx metallib shader.air -o shader.metallib
xcrun -sdk macosx metal-tt shader.air -o shader.gpu.bin   # binary MSL for cache validation
```

- MSL target on this host: `air64-apple-darwin27.0.0`, MSL 4.0, GPU Family Metal 4 / Apple 9.
- Shader-dump compile gates: `MVK_CONFIG_SHADER_DUMP_DIR` (MoltenVK) and `VKD3D_SHADER_DUMP_PATH` (vkd3d-proton) → compile every dumped MSL with `xcrun metal` → zero errors.

## 6. Probe toolchains

- `scripts/mvkprobe.c` — native arm64: `clang -O2 -o mvkprobe mvkprobe.c -I<vulkan-headers>` (headers via brew `/opt/homebrew/include/vulkan`).
- `scripts/flprobe.c` — PE: `x86_64-w64-mingw32-clang -O2 -o flprobe.exe flprobe.c -ldxgi -ld3d12` (llvm-mingw; official D3D12_FEATURE numeric ids — see header pitfalls below).

## 7. Known toolchain pitfalls (from this project's history)

1. **mingw-w64's d3d12.h renumbers the `D3D12_FEATURE` enum** (SHADER_MODEL=7, OPTIONS5=27…). Never use mingw enum names for feature queries — use official Microsoft numeric ids: FEATURE_LEVELS=2, SHADER_MODEL=18, OPTIONS5=19, OPTIONS6=20, OPTIONS7=21. (llvm-mingw's own headers are mingw-w64 too — same rule.)
2. **Mixed d3d12.dll/d3d12core.dll pairs** (different arches or builds) = instant early exit. Always hash both and require `file` = PE32+ x86-64, same build dir.
3. **Bundle self-heal** re-extracts `metalsharp-graphics-dll.tar.zst` whenever a lane ≠ bundle hashes → custom lanes wiped on next backend launch. Iterate with manual wine launches; integrate via bundle + hash pins (M14).
4. **The game loads d3d12/d3d12core from the game dir** (override `n,b`), not the runtime lane — stage the matched pair in the game dir and prove with `lsof`/module path.
5. **ICD pinning**: always `VK_ICD_FILENAMES=<dir>/MoltenVK_icd.json` with the dylib in the same dir (relative `library_path`).
6. **DYLD paths for the Wine runtime**: `DYLD_LIBRARY_PATH`/`DYLD_FALLBACK_LIBRARY_PATH` must include `$MS_RUNTIME/lib/moltenvk-vkmt` and `$MS_RUNTIME/lib/wine/x86_64-unix` (also silences `mscompatdb: not found`).
7. **Rosetta AOT stall** after swapping dylibs — verify the loaded dylib sha; content change forces re-translation.
8. **Never ad-hoc-sign the installed Wine host** for capture experiments; use isolated capture hosts.
