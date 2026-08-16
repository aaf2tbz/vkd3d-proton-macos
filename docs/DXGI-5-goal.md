# DXGI Phase 5 Goal Prompt

## Objective

Complete DXGI Phase 5: synchronization, frame pacing, and deterministic
recovery validation.

Start from the pushed DXGI-4 implementation and add a pinned-lane DXGI/D3D12
synchronization probe and validator using the configured Wine runner. DXVK's
`dxgi.dll` remains the presentation provider; do not begin DXGI-6 packaging or
claim broad gameplay stability.

## Requirements

- Use the Phase-1-validated adapter and the existing DXGI-4 presentation path.
- Create a real Win32 window, D3D12 device, graphics queue, command allocators,
  command lists, fences, and swapchain.
- Exercise two, three, and four frames in flight.
- Validate `ID3D12Fence` signal/completion, `SetEventOnCompletion`, queue
  `Signal`, queue `Wait`, bounded CPU waits, GPU-idle shutdown, and queue
  dependency chains where supported.
- Validate frame-latency waitable objects, `SetMaximumFrameLatency`, present
  sync intervals 0 and 1, tearing, and occlusion behavior.
- Exercise `IDXGIDevice3::Trim` or accurately report it as unavailable.
- Render deterministic RGB-triangle and clear-color frames while pacing is
  controlled by fences and frame-latency waits.
- Verify backbuffer transitions, fence values, present ordering, frame
  completion, and exact GPU readback pixels for every accepted mode.
- Add controlled resource and pipeline churn: buffers, textures, RTVs, SRVs,
  command allocators, graphics pipelines, shader loading/compilation, and
  pipeline-cache behavior. Record resource and memory samples and detect
  monotonic growth.
- Run at least 100,000 frames or 30 minutes, using multiple frames in flight,
  periodic resource churn, pacing changes, and supported lifecycle events.
- Exercise `GetDeviceRemovedReason` after normal operation, timeout paths,
  occlusion, shutdown, and controlled invalid operations.
- Classify HRESULTs as success, timeout, occlusion, unsupported, device
  removal, invalid argument, or backend failure. Retries must be bounded and
  logged; never turn a timeout or device loss into success.
- If the macOS/Metal backend cannot induce or recover from device removal,
  report that limitation explicitly instead of claiming recovery support.

## Negative tests

Add deterministic tests for:

- an unsignaled fence with a bounded timeout;
- invalid fence/event values;
- invalid queue waits;
- duplicate or out-of-order fence values;
- frame-latency timeout;
- present after queue/device teardown;
- releasing a resource before GPU completion;
- device-removed-reason queries after normal and failed shutdown; and
- bounded handling of an accepted transient failure.

## Files and commands

Add:

- `scripts/probes/dxgi-sync/`
- `scripts/validate-dxgi-phase5.sh`
- `make dxgi-sync-probe`
- `make dxgi-sync-test`

Capture complete evidence under `artifacts/evidence/`, including staged module
paths, PE/Mach-O types, SHA-256 hashes, DXVK/vkd3d-proton/MoltenVK revisions,
compiler/SDK/Wine/Vulkan/runtime versions, Wine and backend logs, frame counts,
fence timelines, wait durations, timeout results, memory/resource samples, and
final HRESULT classifications.

## Regression and documentation gates

- Run at least two independent short validator passes and one long stress pass.
- Preserve DXGI-1, DXGI-2, DXGI-3, DXGI-4, and the six-probe regression suites
  at 100% green.
- Update `docs/DXGI-Roadmap.md`, `docs/Development.md`, `docs/validation.md`,
  `docs/Final.md`, and `ROADMAP.md`.
- Commit all Phase-5 source, scripts, tests, documentation, and evidence, then
  push the completed phase to `main`.

## Acceptance criteria

The phase passes only when the accepted stress run has no crash, hang,
deadlock, infinite retry, device removal, corrupted frame, stale drawable, or
use-after-free; fence completion and CPU/GPU waits are deterministic and
bounded; every accepted pacing mode remains pixel-correct; memory and resource
usage remain bounded; unsupported recovery behavior is reported accurately; two
short runs are repeatable; the long stress gate passes; and all prior DXGI and
six-probe regression gates remain green.

Do not begin DXGI-6 packaging or real-game acceptance. Do not claim broad
gameplay stability from synthetic stress results. Do not claim device-loss
recovery unless the configured Wine/DXVK/vkd3d/MoltenVK backend demonstrates it.
