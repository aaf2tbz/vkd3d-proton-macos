# Validation and regression

## Runtime environment

The probes run under a compatible Wine installation on the target macOS
host, with the candidate files staged beside the probe executable. For the
compatibility claim, the host must be macOS 14 / Metal 3; running on a newer
host is only a newer-host smoke test.

```bash
source scripts/env.sh
export WINEPREFIX="$WS/artifacts/prefix"
export WINE_BIN="${WINE_BIN:-$(command -v wine)}"
export WINEDLLOVERRIDES="d3d12,d3d12core,dxgi=n,b"
export VK_ICD_FILENAMES="$WS/artifacts/stage-dxr/MoltenVK_icd.json"
export DYLD_LIBRARY_PATH="$WS/artifacts/stage-dxr${WINE_UNIX_LIB:+:$WINE_UNIX_LIB}"
export DYLD_FALLBACK_LIBRARY_PATH="$DYLD_LIBRARY_PATH"
```

The exact local runner may add `VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT=1`,
`MVK_PRESENT_MODE=1`, cache paths, and `WINEDEBUG=-all`. Keep those settings
in the runner rather than changing the probes.

## DXGI-1 adapter gate

The pinned DXVK macOS DXGI provider and adapter identity gate run with:

```bash
make dxgi-test
```

This proves native `dxgi.dll` loading, DXGI factory creation, hardware adapter
enumeration, stable DXGI/D3D12 LUID matching, D3D12Core loadability and exports,
Vulkan/MoltenVK vendor/device identity, ten repeatable runs, invalid-input
handling, and the complete regression suite. It does not create a swapchain;
presentation begins in DXGI-2.

## DXGI-2 windowed presentation gate

Run the complete Phase 2 gate with:

```bash
make dxgi-present-test
```

The validator requires a native x86_64 DXVK `dxgi.dll` built from the pinned
base plus the checked-in D3D12 bridge patch, a real Win32 window, and a
DXGI/D3D12 LUID match. It runs two deterministic passes of all four accepted
windowed combinations:

- `CreateSwapChain` with flip-discard and flip-sequential, followed by `Present`;
- `CreateSwapChainForHwnd` with flip-discard and flip-sequential, followed by
  `Present1`.

Every accepted mode renders a deterministic clear and triangle, checks
backbuffer transitions and CPU readback, runs 1,000 frames, alternates sync
intervals 0/1, requests `DXGI_PRESENT_ALLOW_TEARING` only when reported,
executes `DXGI_PRESENT_TEST`, and checks frame statistics and last-present
count. Invalid descriptors and post-release presentation are negative tests.
The validator then reruns the six-probe regression suite and records module
paths, SHA-256 hashes, source revisions, and Wine/DXVK/vkd3d/MoltenVK output in
`artifacts/evidence/dxgi-2-presentation.md` and its adjacent logs.

Phase 2 does not cover resize, minimize, fullscreen, recovery stress, or broad
gameplay stability. Those claims remain blocked until later phase gates pass.

## Probe gate

The M14 gate is green only when all of these pass:

| Probe | Required result |
|---|---|
| `flprobe.exe` | all six feature-level rungs create; max 12_2 |
| `cr_inner_probe.exe` | CR tier-3 InnerCoverage result matches reference |
| `feedback_probe.exe` | sampler feedback matches CPU reference exactly |
| `mesh_probe.exe` | mesh dispatch is pixel-exact |
| `corpus.exe` | CORE_1_0 corpus works |
| `corpus_gs.exe` | geometry-shader corpus works |
| `compute_matrix.exe` | CORE_1_0 compute matrix works |

Run an individual staged probe with the established runner:

```bash
cd artifacts/stage-dxr
/tmp/run-probe.sh flprobe.exe
/tmp/run-probe.sh feedback_probe.exe
/tmp/run-probe.sh mesh_probe.exe
```

## Evidence requirements

Each promoted runtime must record:

1. `d3d12.dll`, `d3d12core.dll`, and `libMoltenVK.dylib` SHA-256 hashes;
2. the source commit IDs and compiler versions;
3. the loaded module paths from the probe;
4. the full ladder and regression output; and
5. MoltenVK architecture and code-signature checks.

The authoritative M14 ladder evidence is
`artifacts/evidence/rung-ladder-2026-08-16.txt`. The release archive repeats
its own SHA-256 checksums so a downloaded runtime can be checked independently.

## Failure rules

- A stale staged DLL invalidates the run; rebuild and restage both DLLs.
- A mixed-architecture or mixed-build D3D12 pair is invalid.
- Extension advertisement or a successful shader translation is not GPU
  acceptance evidence without deterministic readback.
- Do not use `VKD3D_FEATURE_LEVEL` or option-bit forcing to claim support.
