# VKD3D-Proton-MacOS — Workspace

Goal: full D3D12 feature-level support (11_0 → 12_2 + CORE_1_0) on the MetalSharp route
`vkd3d-proton → Vulkan → custom MoltenVK → Metal` under MetalSharp Wine 11.5.

**Start here:** [`ROADMAP.md`](ROADMAP.md) — the master plan (milestones M0–M14, rung-by-rung work items, risk register).

## Layout

```
VKD3D-Proton-MacOS/
├── ROADMAP.md                      # master roadmap (extensive)
├── README.md                       # this file
├── docs/
│   ├── 01-feature-level-evidence.md  # Phase 0 measurements, hashes, verdicts
│   ├── 02-build-toolchain.md         # toolchain inventory + build recipes + pitfalls
│   └── 03-validation-harnesses.md    # probe design + runbook
├── toolchain/
│   ├── llvm-mingw-20260616-ucrt-macos-universal/  # clang 22.1.8 (matches shipped build)
│   ├── llvm-mingw -> llvm-mingw-20260616-ucrt-macos-universal/bin
│   ├── ninja / meson / cmake       # symlinks to Homebrew tools
│   └── Xcode-beta.app -> /Users/averyfelts/Downloads/Xcode-beta.app
├── sources/                        # FRESH clones + our custom baseline
│   ├── vkd3d-proton/  ├── MoltenVK/          # fresh upstream (build base)
│   ├── SPIRV-Cross/   # fresh upstream, pinned into MoltenVK builds
│   ├── MetalSharp/    # fresh upstream (PR base)
│   └── moltenvk-vkmt-custom-bundle/          # OUR custom MoltenVK from the bundle
│       └── Graphics/dll/moltenvk-vkmt/{libMoltenVK.dylib, MoltenVK_icd.json}
│                           # sha 50e41de2… — the canonical working artifact until
│                           # the rebuilt custom source surpasses it (baseline for probes)
├── scripts/
│   ├── env.sh                      # workspace env (PATH, DEVELOPER_DIR, MS_WINE)
│   ├── validate-toolchain.sh       # one-shot toolchain verification
│   ├── build-vkd3d-proton.sh       # llvm-mingw PE build (d3d12.dll + d3d12core.dll)
│   ├── build-moltenvk.sh           # universal MoltenVK dylib build
│   ├── mvkprobe.c / mvkprobe       # native Vulkan capability + functional gate
│   └── flprobe.c                   # D3D12 feature-level probe (PE, wine)
└── artifacts/
    ├── build/                      # vkd3d-proton build outputs + moltenvk-vkmt
    │   └── moltenvk-vkmt/          #   = custom bundle dylib baseline (sha 50e41de2…)
    ├── stage/                      # probe staging (DLLs + exe)
    ├── evidence/                   # dated evidence captures
    └── caches/                     # VKD3D/DXVK cache dirs (isolated)
```

## Quickstart

```bash
cd /Volumes/AverySSD/VKD3D-Proton-MacOS
source scripts/env.sh
scripts/validate-toolchain.sh      # verifies llvm-mingw/ninja/meson/cmake/Xcode/metal/wine
scripts/build-vkd3d-proton.sh      # builds the PE pair into artifacts/build/
scripts/build-moltenvk.sh          # builds the universal dylib into artifacts/build/
# probes per docs/03-validation-harnesses.md
```

## State (2026-08-14)

- Phase 0 evidence locked (docs/01) — current shipped pair = **feature level 11_0 only**.
- M0 (workspace + toolchain): llvm-mingw 20260616 (exact shipped compiler), Xcode 27b4, CLT b5, ninja/meson/cmake — set up; fresh clones in progress.
- Next: M1 harness parity (headless device creation in the exact M12 env).
