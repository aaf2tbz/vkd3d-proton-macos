# DXGI Stability Roadmap

**Status:** DXGI-4 complete; DXGI-5 synchronization/recovery work is next.

The current release proves device creation, off-screen D3D12 rendering, shader
translation, and deterministic readback. It does **not** yet prove a stable
windowed game path. That path is:

```text
D3D12 game
  -> DXVK macOS dxgi.dll
  -> Wine Vulkan / DXVK presentation
  -> MoltenVK
  -> Metal 3 (macOS 14)
```

DXGI is therefore a first-class runtime component, not just a launcher detail.
The existing package relies on the user's compatible Wine/DXGI installation;
this roadmap moves DXVK macOS into a pinned, buildable, testable workspace
lane and decides whether the resulting `dxgi.dll` is bundled with a later
runtime release.

### DXGI-1 completion record

Phase 1 passed on 2026-08-16. The pinned source is Gcenx/DXVK-macOS commit
`8f1e28deed3ad30802f7e1bdff428ec14e6e7817`, and `make dxgi` now produces the
native x86_64 `dxgi.dll`. The adapter probe, ten-run repeatability gate,
negative tests, module provenance checks, and existing six-probe regression
suite are recorded in
[`artifacts/evidence/dxgi-1-adapter-identity.md`](../artifacts/evidence/dxgi-1-adapter-identity.md).
This closes adapter identity only; it does not claim swapchain or gameplay
stability.

### DXGI-2 completion record

Phase 2 passed on 2026-08-16. The pinned DXVK macOS base remains commit
`8f1e28deed3ad30802f7e1bdff428ec14e6e7817`; the D3D12 windowed bridge is the
checked-in patch `patches/dxvk-macos-d3d12-dxgi.patch`. The native provider now
consumes vkd3d-proton's Vulkan-backed swapchain factory, creates a real Win32
surface, and exposes the D3D12 backbuffers through the DXGI frontend.

`make dxgi-present-test` passed two deterministic runs of all four accepted
combinations: `CreateSwapChain`/`CreateSwapChainForHwnd` × flip-discard/
flip-sequential. Each mode rendered and read back the deterministic clear and
triangle for 1,000 frames, exercised sync intervals 0 and 1, tearing, both
`Present` forms, `DXGI_PRESENT_TEST`, frame statistics, last-present count, and
the invalid-descriptor/post-release negative tests. The six-probe regression
suite remained green. Full hashes and logs are in
[`artifacts/evidence/dxgi-2-presentation.md`](../artifacts/evidence/dxgi-2-presentation.md).

This closes basic windowed presentation only. It does **not** claim resize,
minimize, fullscreen, long-run recovery, or broad gameplay stability; those
remain later phase gates.

### DXGI-3 completion record

Phase 3 passed on 2026-08-16 with `make dxgi-lifecycle-test`. The probe uses a
real Win32 window and the Phase-1 adapter, renders the RGB triangle and clear
color with GPU readback after every accepted operation, and verifies
PRESENT/render-target/COPY_SOURCE/PRESENT transitions. It passed normal
multi-size `ResizeBuffers`, zero-size minimized behavior with explicit
unsupported classification where no drawable exists, occlusion/test Present,
fullscreen query and windowed fallback, window destruction/recreation, invalid
parameters, an outstanding-backbuffer-reference negative case, and 100
create/resize/destroy cycles. It also checks ordered shutdown and was
repeatable across two runs. The vkd3d lifecycle dimension guard is the checked-
in patch `patches/vkd3d-proton-dxgi-lifecycle.patch`, applied only during the
build and reverted afterwards.

The DXGI-1, DXGI-2, and six-probe suites were rerun by the Phase-3 validator.
Full module hashes, source revisions, Wine/DXVK/vkd3d/MoltenVK output, and
acceptance logs are in
[`artifacts/evidence/dxgi-3-lifecycle.md`](../artifacts/evidence/dxgi-3-lifecycle.md).
This closes the tested lifecycle lane only; it does **not** claim broad
gameplay stability, later format/HDR coverage, or a final package promotion.

### DXGI-4 completion record

Phase 4 passed on 2026-08-16 with `make dxgi-formats-test`. The pinned lane
rendered and read back `B8G8R8A8_UNORM`, `B8G8R8A8_UNORM_SRGB`, and
`R8G8B8A8_UNORM` swapchains with exact channel/alpha values, including the
sRGB transfer results. It validated D3D12 format support, resource/RTV
creation, descriptors, barriers, an MSAA `ResolveSubresource`, backbuffer
acquisition, presentation, and tearing policy. `R10G10B10A2_UNORM` is reported accurately: D3D12 advertises
the format and an RTV can be created, but this DXGI/MoltenVK lane rejects its
swapchain and does not attempt unsafe GPU readback.

The D24/D32 depth-stencil cases create resources and DSVs, clear depth and
stencil, exercise barriers, and read back the depth plane. The current
backend's copy footprint does not expose a stencil readback plane, which is
recorded as unsupported rather than synthesized. SDR P709 is supported;
scRGB, HDR10 PQ, and extended P2020 are reported unsupported by the configured
DXGI path on this run. A successful HDR metadata setter call is not treated as
HDR support without `CheckColorSpaceSupport` and deterministic output.

Two format/color runs passed, and the DXGI-1/2/3 plus six-probe suites were
rerun. Complete hashes, revisions, module paths, and logs are in
[`artifacts/evidence/dxgi-4-formats.md`](../artifacts/evidence/dxgi-4-formats.md).
This closes format and policy coverage only; it does not claim HDR support or
broad gameplay stability.

## Rules for every phase

- Use a fresh, pinned DXVK macOS source tree under `sources/`.
- Keep `dxgi.dll`, `d3d12.dll`, and `d3d12core.dll` from one validated build
  lane; never mix timestamps or source revisions.
- Test the real native DXGI override with `WINEDLLOVERRIDES`, not only a
  stubbed or builtin DLL.
- Require GPU execution and deterministic readback; capability advertisement
  alone is not evidence.
- Record source revisions, compiler versions, loaded module paths, and SHA-256
  hashes for every promoted artifact.
- Run the complete existing regression suite after every phase.
- A newer Apple host is useful for development, but the compatibility exit
  gate requires a real macOS 14 / Metal 3 host.

## Phase 1 — Adapter discovery and identity

**Goal:** DXGI and D3D12 select the same physical Vulkan/MoltenVK adapter.

### Work

- Add or import the pinned DXVK macOS source and build configuration.
- Build the native `dxgi.dll` for the supported Wine/Rosetta runtime.
- Implement a `dxgi_probe.exe` that exercises:
  - `CreateDXGIFactory`, `CreateDXGIFactory1`, and `CreateDXGIFactory2`;
  - `EnumAdapters` / `EnumAdapters1` and adapter descriptions;
  - adapter LUIDs, vendor/device IDs, memory sizes, and outputs; and
  - D3D12 device creation from the selected adapter.
- Compare the DXGI adapter identity with the D3D12/Vulkan physical-device
  identity. Fix LUID mapping before touching presentation.

### Exit gate

The probe must enumerate the intended Apple adapter, create D3D12 from that
adapter, report stable identity across repeated launches, and reject or clearly
handle unsupported adapters. No `d3d12_find_physical_device` mapping errors may
remain in the validated log.

## Phase 2 — Window and swapchain presentation

**Goal:** present a rendered frame reliably through DXVK macOS and MoltenVK.

### Work

- Create a minimal Win32 window under Wine.
- Exercise `CreateSwapChain` and `CreateSwapChainForHwnd` with flip-discard
  and flip-sequential descriptors.
- Render a clear and a triangle through the D3D12 path, then call `Present`
  and `Present1` for at least 1,000 frames.
- Validate vsync, `sync_interval`, `DXGI_PRESENT_TEST`, tearing flags, and
  frame pacing where the host supports them.
- Verify drawable acquisition, back-buffer state transitions, and release
  ordering in DXVK/MoltenVK.

### Exit gate

A pixel-verified windowed presentation probe runs for 1,000 frames without a
hang, device removal, validation error, or leaked swapchain resources. Both
flip modes required by the probe must pass, or the unsupported mode must be
reported accurately and tested out of the game compatibility claim.

## Phase 3 — Resize, minimize, and fullscreen lifecycle

**Goal:** survive the window events games perform during ordinary gameplay.

### Work

- Exercise `ResizeBuffers` across zero/minimized and normal dimensions.
- Recreate render targets after resize and verify their dimensions and formats.
- Test minimize/restore, occlusion, window destruction, and repeated
  swapchain create/destroy cycles.
- Test `SetFullscreenState`, `GetFullscreenState`, `ResizeTarget`, and safe
  windowed fallback. Keep exclusive fullscreen optional on macOS.
- Verify shutdown in the correct order: GPU idle, views, buffers, swapchain,
  queue, device, factory, and window.

### Exit gate

The lifecycle probe completes repeated resize/minimize/fullscreen cycles with
pixel-correct output and no crash, deadlock, stale drawable, or use-after-free.
It must also pass a stress loop of at least 100 create/resize/destroy cycles.

## Phase 4 — Formats, color, HDR, and presentation policy

**Goal:** cover the render-target formats and presentation options used by
real games.

### Work

- Validate `B8G8R8A8_UNORM`, `B8G8R8A8_UNORM_SRGB`,
  `R8G8B8A8_UNORM`, and the common depth/stencil combinations.
- Add `R10G10B10A2_UNORM` and HDR/extended-range tests where macOS 14 and the
  physical display support them; report unsupported combinations honestly.
- Test format support queries, render-target views, barriers, resolves, and
  readback for every accepted format.
- Validate sRGB conversion, alpha mode, color space, and `Present` behavior.
- Test tearing and frame-latency settings without enabling unsupported modes.

### Exit gate

Every advertised format/presentation combination has a deterministic render
and readback row. Unsupported HDR, tearing, or fullscreen combinations return
documented results instead of silently producing corrupted frames.

## Phase 5 — Synchronization, pacing, and recovery

**Goal:** make long-running gameplay stable rather than merely able to show a
few frames.

### Work

- Exercise fences, event completion, queue waits, frame-latency objects, and
  CPU/GPU pacing across multiple frames in flight.
- Verify `Flush`, wait behavior, occlusion handling, and clean queue shutdown.
- Exercise `GetDeviceRemovedReason` and classify expected Wine/DXVK/MoltenVK
  errors without turning transient conditions into hangs.
- Add repeated present stress with shader compilation and resource churn.
- Capture Metal/Vulkan validation output and Wine loader logs for failures.

### Exit gate

The stress probe runs for at least 30 minutes or 100,000 frames, including
resource churn and resize events, without deadlock, runaway memory growth,
device loss, or presentation corruption. Recovery paths are deterministic and
logged.

## Phase 6 — Packaging and real-game acceptance

**Goal:** prove that the complete DXGI+D3D12 runtime supports gameplay and can
be shipped reproducibly.

### Work

- Add first-class Make targets for the DXVK macOS lane, for example:
  `make dxgi`, `make dxgi-probe`, `make dxgi-test`, and `make dxgi-package`.
- Stage and hash `dxgi.dll` beside the matched D3D12 pair. Decide, based on
  licensing and Wine coupling, whether it belongs in the public archive or
  remains a separately installed dependency.
- Extend the package README with exact DLL override order, supported Wine
  requirements, and known DXGI limitations.
- Run the complete probe suite plus the DXGI presentation/lifecycle suite.
- Perform smoke tests with representative D3D12 games covering:
  - borderless/windowed presentation;
  - resize and alt-tab;
  - shader compilation and pipeline cache creation;
  - streaming/resource churn;
  - controller/input behavior; and
  - any required DXR, mesh, sampler-feedback, or VRS path.
- Repeat the acceptance matrix on a real macOS 14 / Metal 3 host.

### Exit gate

The runtime is gameplay-ready only when all five earlier phases are green, the
full regression suite remains green, and representative games complete a
repeatable launch-to-gameplay session on macOS 14. The release must include
module provenance, hashes, installation instructions, and an explicit list of
features not supported by the selected DXVK/Wine lane.

## Dependency order and milestone names

| Milestone | Phase | Deliverable |
|---|---:|---|
| DXGI-1 | 1 | Pinned DXVK macOS source, native build, adapter/LUID probe |
| DXGI-2 | 2 | Windowed flip swapchain and 1,000-frame present probe |
| DXGI-3 | 3 | Resize/minimize/fullscreen lifecycle stress probe |
| DXGI-4 | 4 | Format, sRGB, HDR, tearing, and color-space matrix |
| DXGI-5 | 5 | Fence/pacing/recovery stress and long-run stability evidence |
| DXGI-6 | 6 | Bundled/dependency decision, game matrix, macOS 14 release gate |

The phases are sequential. Phase 4 can develop format rows in parallel with
late Phase 3 work, but it cannot be promoted until adapter identity and basic
presentation are green. Phase 6 is blocked by any unresolved Phase 1–5 issue.
