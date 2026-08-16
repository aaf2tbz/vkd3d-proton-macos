# Validation Harnesses — Design & Runbook

> This file contains the original harness design. The M14 gate is complete;
> use [validation.md](validation.md) for the current probe list, environment,
> and acceptance rules.

Two probe layers + game acceptance. Every promoted feature needs a **GPU-executed row with deterministic readback** — advertisement, enumeration, or compile success is never enough.

## 1. `mvkprobe` — native Vulkan gate (dlopen, no ICD ambiguity)

- dlopens an exact `libMoltenVK.dylib` path → capability dump (Phase 0) and **functional rows** (per rung).
- Contract per row: exact extension/feature required, real operation (pipeline+submit+readback), sentinel-verified output bytes, `PASS`/`FAIL` + candidate hash.
- Rows to add per rung:
  - Rung 1 (logicOp): render 16 logic ops × {RGBA8, RGBA16F, RGBA32F, RGB10A2} × {no-MSAA, 4xMSAA}; readback pixel-exact vs CPU reference.
  - Rung 2 (sparse): bind sparse image tiles, map/unmap residency, draw, verify page-in/page-out and NULL-resident reads; `vkQueueBindSparse`.
  - Rung 3 (ROV/CR): fragment interlock ordering stress; conservative-raster coverage buffer (outer coverage pixel mask), degenerate triangle rows at tier 2/3.
  - Rung 4A (RT): RT triangle + procedural sphere, closest/any-hit attributes, inline RayQuery in compute, TLAS instancing.
  - Rung 4B (mesh): meshlet cube with per-primitive payloads, task/amplification culling, indexed mesh draws.
  - Rung 4C (VRS): 2x2/2x4 shading-rate image, region rate transitions, combiner matrix, per-primitive (if feasible).
  - Rung 4D: copy-queue timestamps + query copy; sampler feedback (int64 image atomics); depth bounds test.
- Compile: `clang -O2 -o mvkprobe mvkprobe.c -I/opt/homebrew/include`.

## 2. `flprobe` — D3D12 gate under Wine 11.5

- PE probe (llvm-mingw) staged in a dir with the candidate `d3d12.dll` + `d3d12core.dll` + DXVK `dxgi.dll`.
- Official-numeric `D3D12_FEATURE` ids only (mingw renumbers — see toolchain doc).
- Phase 0 output (already captured): module identity, adapter enumeration, per-level `D3D12CreateDevice` HRESULTs, `CheckFeatureSupport` for FEATURE_LEVELS / OPTIONS / SHADER_MODEL / OPTIONS5 (DXR) / OPTIONS6 (VRS) / OPTIONS7 (mesh+sampler feedback) / ARCHITECTURE.
- **M1 blocker:** device creation returned `E_INVALIDARG` on the bare-wine env (custom core D3DKMT adapter path) — repair env to the exact M12 shape and make headless creation work before trusting per-rung HRESULTs.
- Runbook:

```bash
source scripts/env.sh
export WINEPREFIX=$WS/artifacts/prefix WINEDEBUG=-all
export WINEDLLOVERRIDES="d3d12,d3d12core,dxgi=n,b"
export VK_ICD_FILENAMES=$WS/artifacts/build/moltenvk-vkmt/MoltenVK_icd.json
export DYLD_LIBRARY_PATH=$MS_RUNTIME/lib/moltenvk-vkmt:$MS_RUNTIME/lib/wine/x86_64-unix
export DYLD_FALLBACK_LIBRARY_PATH=$DYLD_LIBRARY_PATH
export VKD3D_SHADER_CACHE_PATH=$WS/artifacts/caches/vkd3d
export DXVK_STATE_CACHE_PATH=$WS/artifacts/caches/dxvk
export VKD3D_DEBUG=warn   # trace for ladder verification
cd $WS/artifacts/stage && $MS_WINE ./flprobe.exe
```

- Ladder proof: `VKD3D_DEBUG=trace` → expect `TRACE:vkd3d-proton:d3d12_device_caps_init_feature_level: Max feature level: 0x____`.

## 3. Shader compile gate (MSL)

- Dump shaders (`MVK_CONFIG_SHADER_DUMP_DIR`, `VKD3D_SHADER_DUMP_PATH`), compile every `.metal` with `xcrun -sdk macosx metal` (Xcode 27b4), zero-error policy; metallib + metal-tt pass for cache validation.

## 4. Game acceptance (final per milestone)

- Control (870780) — D3D12, the M12 reference game; windowed 320x200 probe mode via `scripts/probe-game-exe-m12.sh` equivalent: exit code, MSL-codegen error count, distinct error lines, swapchain count, mvk-error count.
- Schedule I — D3D11-via-M12 (DXVK d3d11) regression.
- 12_2 games (post-M12): a DXR title + a mesh-shader/VRS title if available in library.

## 5. Evidence recording (per run)

- Candidate hashes (d3d12.dll, d3d12core.dll, libMoltenVK.dylib), commit hashes, probe output digest, loaded-module paths, env pins — all appended to `docs/01-feature-level-evidence.md` (or a dated evidence file under `artifacts/evidence/`).
- Provenance check after any game launch: post-run sha compare of the candidate vs runtime lane vs game dir + `injections.json matches_source`.
