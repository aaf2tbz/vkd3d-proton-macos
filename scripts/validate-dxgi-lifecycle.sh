#!/bin/bash
# DXGI-3 resize, minimize, fullscreen, destruction, and lifecycle gate.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh" >/dev/null
STAGE="${STAGE_DIR:-$WS/artifacts/stage-dxr}"
RUNNER="${WINE_RUNNER:-/tmp/run-probe.sh}"
PROBE="$STAGE/dxgi_lifecycle_probe.exe"
DXVK_SRC="${DXVK_SRC:-$WS/sources/dxvk-macos}"
DXVK_COMMIT="${DXVK_COMMIT:-8f1e28deed3ad30802f7e1bdff428ec14e6e7817}"
DXVK_PATCH="${DXVK_PATCH:-$WS/patches/dxvk-macos-d3d12-dxgi.patch}"
EVIDENCE="$WS/artifacts/evidence"
TMP="$(mktemp -d /tmp/dxgi-lifecycle.XXXXXX)"

restore_previous_stage_evidence() {
    [ -d "$TMP/previous-stage-evidence" ] || return 0
    for old_evidence in "$EVIDENCE"/dxgi-1-* "$EVIDENCE"/dxgi-2-*; do
        [ -f "$old_evidence" ] || continue
        rm -f "$old_evidence"
    done
    for old_evidence in "$TMP"/previous-stage-evidence/*; do
        [ -f "$old_evidence" ] || continue
        cp "$old_evidence" "$EVIDENCE/"
    done
}
trap 'restore_previous_stage_evidence; rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[ -x "$RUNNER" ] || fail "WINE_RUNNER is not executable: $RUNNER"
[ -f "$PROBE" ] || fail "DXGI-3 probe missing: $PROBE"
[ -d "$DXVK_SRC/.git" ] || fail "DXVK source is not a git checkout: $DXVK_SRC"
[ "$(git -C "$DXVK_SRC" rev-parse HEAD)" = "$DXVK_COMMIT" ] || fail "DXVK base commit mismatch"
git -C "$DXVK_SRC" diff --quiet || fail "DXVK source has uncommitted changes"
[ -s "$DXVK_PATCH" ] || fail "DXVK D3D12 bridge patch missing: $DXVK_PATCH"

for file in dxgi.dll d3d12.dll d3d12core.dll; do
    [ -f "$STAGE/$file" ] || fail "staged module missing: $file"
    file "$STAGE/$file" | grep -q 'PE32+.*x86-64' || fail "$file is not x86_64 PE32+"
done
[ -f "$STAGE/libMoltenVK.dylib" ] || fail "staged MoltenVK missing"
[ -f "$STAGE/MoltenVK_icd.json" ] || fail "staged MoltenVK ICD missing"

mkdir -p "$TMP/dxvk"
export DXVK_LOG_PATH="$TMP/dxvk"

run_lifecycle_probe() {
    local run="$1"
    local raw="$TMP/lifecycle-$run.raw"
    local log="$TMP/lifecycle-$run.log"
    "$RUNNER" dxgi_lifecycle_probe.exe >"$raw" 2>&1 || fail "DXGI-3 probe run $run exited nonzero"
    tr -d '\r' <"$raw" >"$log"
    grep -q '^DXGI-3 result: PASS (0 failures)$' "$log" || fail "DXGI-3 probe run $run failed"
    grep -q '^selected adapter:' "$log" || fail "DXGI adapter selection missing on run $run"
    # Wine's asynchronous vkd3d logger can legally write between the two
    # halves of this printf.  The probe only reaches PASS after memcmp has
    # matched the LUIDs, so accept either an intact line or the split suffix.
    grep -q '^D3D12 adapter LUID:' "$log" || fail "D3D12 adapter identity line missing on run $run"
    grep -q ' (MATCH)$' "$log" || fail "DXGI/D3D12 adapter identity mismatch on run $run"
    grep -q '^window/device/queue: PASS$' "$log" || fail "window/device/queue setup missing on run $run"
    grep -q '^  create lifecycle swapchain: hr=.* PASS$' "$log" || fail "swapchain creation missing on run $run"
    [ "$(grep -c '^  readback .*: PASS$' "$log")" -ge 8 ] || fail "not all lifecycle readbacks passed on run $run"
    for size in '800,600' '320,240' '1024,512'; do
        grep -q "^  ResizeBuffers($size): hr=.* PASS$" "$log" || fail "resize $size missing on run $run"
    done
    grep -q '^  invalid resize dimensions: hr=.* PASS$' "$log" || fail "invalid resize negative missing on run $run"
    grep -q '^  recover after invalid resize: hr=.* PASS$' "$log" || fail "invalid resize recovery missing on run $run"
    grep -q '^  resize with outstanding backbuffer reference: hr=.* PASS$' "$log" || fail "outstanding-reference negative missing on run $run"
    grep -q '^  invalid ResizeTarget(NULL): hr=.* PASS$' "$log" || fail "invalid target negative missing on run $run"
    grep -q '^  minimized occlusion/test Present: hr=.* PASS$' "$log" || fail "minimize/occlusion coverage missing on run $run"
    grep -q '^  minimized ResizeBuffers(0,0): hr=.* \(SUPPORTED\|UNSUPPORTED\)$' "$log" || fail "zero-size resize was not classified on run $run"
    grep -q '^  GetFullscreenState(initial): hr=.* PASS$' "$log" || fail "fullscreen query missing on run $run"
    grep -q '^  SetFullscreenState(TRUE): hr=.* \(SUPPORTED\|UNSUPPORTED\)$' "$log" || fail "fullscreen support was not classified on run $run"
    grep -q '^  ResizeTarget(lifecycle): hr=.* \(PASS\|UNSUPPORTED\)$' "$log" || fail "ResizeTarget coverage missing on run $run"
    grep -q '^  SetFullscreenState(FALSE) fallback: hr=.* PASS$' "$log" || fail "windowed fullscreen fallback failed on run $run"
    grep -q '^  Present after window destruction: hr=.* PASS$' "$log" || fail "post-destruction presentation negative missing on run $run"
    for cycle in 25 50 75 100; do
        grep -q "^  create/resize/destroy cycles: $cycle / 100$" "$log" || fail "cycle marker $cycle missing on run $run"
    done
    local order=("shutdown: GPU idle" "shutdown: RTVs/resources" "shutdown: swapchain" "shutdown: queue/fence" "shutdown: device" "shutdown: adapter" "shutdown: factory" "shutdown: window")
    local previous=0 line item
    for item in "${order[@]}"; do
        line="$(grep -n "^  $item$" "$log" | tail -1 | cut -d: -f1)"
        [ -n "$line" ] || fail "shutdown marker missing: $item"
        [ "$line" -gt "$previous" ] || fail "shutdown order invalid at: $item"
        previous="$line"
    done
    if grep -Eqi 'Unhandled page fault|Assertion failed|device removed|deadlock|segmentation fault' "$log"; then
        fail "runtime crash/error signature in lifecycle run $run"
    fi
}

run_lifecycle_probe 1
run_lifecycle_probe 2

summary_pattern='^(selected adapter:|D3D12 adapter LUID:|tearing support:|window/device/queue:|=== |  create lifecycle|  desc:|  readback|  Present after|  ResizeBuffers|  invalid |  recover |  minimized |  GetFullscreen|  SetFullscreen|  ResizeTarget|  create/resize/destroy cycles:|  shutdown:|DXGI-3 result:|[0-9a-f]{8}-[0-9a-f]{8} \(MATCH\)$)'
repeat_pattern='^(selected adapter:|tearing support:|window/device/queue:|=== |  create lifecycle|  desc:|  readback|  Present after|  ResizeBuffers|  invalid |  recover |  minimized ResizeBuffers|  minimized dimensions|  GetFullscreen|  SetFullscreen|  ResizeTarget|  create/resize/destroy cycles:|  shutdown:|DXGI-3 result:)'
cmp -s <(grep -E "$repeat_pattern" "$TMP/lifecycle-1.log") \
    <(grep -E "$repeat_pattern" "$TMP/lifecycle-2.log") \
    || fail "lifecycle result was not repeatable"
pass "two deterministic DXGI-3 lifecycle runs"

# Re-run the earlier gates, rather than treating a new lifecycle probe as a
# proxy for adapter identity or windowed presentation correctness.
mkdir -p "$TMP/previous-stage-evidence"
for old_evidence in "$EVIDENCE"/dxgi-1-* "$EVIDENCE"/dxgi-2-*; do
    [ -f "$old_evidence" ] || continue
    cp "$old_evidence" "$TMP/previous-stage-evidence/"
done
bash "$WS/scripts/validate-dxgi-adapter.sh" >"$TMP/adapter-validator.log" 2>&1 \
    || { cat "$TMP/adapter-validator.log" >&2; fail "DXGI-1 gate regressed"; }
bash "$WS/scripts/validate-dxgi-presentation.sh" >"$TMP/presentation-validator.log" 2>&1 \
    || { cat "$TMP/presentation-validator.log" >&2; fail "DXGI-2 gate regressed"; }
# The nested validators intentionally write their own historical evidence.
# Preserve that evidence's committed form here; the fresh gate output is
# retained as dxgi-3-stage{1,2}-gate.log and summarized below.
restore_previous_stage_evidence
pass "DXGI-1 and DXGI-2 gates"

for probe in cr_inner_probe.exe feedback_probe.exe mesh_probe.exe corpus.exe corpus_gs.exe compute_matrix.exe; do
    raw="$TMP/$probe.raw"
    log="$TMP/$probe.log"
    "$RUNNER" "$probe" >"$raw" 2>&1 || fail "regression failed: $probe"
    tr -d '\r' <"$raw" >"$log"
    grep -Eq 'RESULT:.*(WORKS|EXACTLY|PIXEL-EXACT)|WORKS' "$log" || fail "regression has no success marker: $probe"
    if grep -Eq 'RESULT:.*FAIL' "$log"; then fail "regression contains a failed result marker: $probe"; fi
    grep -E 'RESULT:|WORKS' "$log" | tail -1 | sed "s#^#$probe: #" >> "$TMP/regression-summary.txt"
done
pass "existing six-probe regression suite"

mkdir -p "$EVIDENCE"
cp "$TMP/lifecycle-1.log" "$EVIDENCE/dxgi-3-probe-run.txt"
cp "$TMP/lifecycle-2.log" "$EVIDENCE/dxgi-3-probe-repeat.txt"
cp "$TMP/adapter-validator.log" "$EVIDENCE/dxgi-3-adapter-gate.log"
cp "$TMP/presentation-validator.log" "$EVIDENCE/dxgi-3-presentation-gate.log"
cp "$TMP/regression-summary.txt" "$EVIDENCE/dxgi-3-regression-summary.txt"
for probe in cr_inner_probe.exe feedback_probe.exe mesh_probe.exe corpus.exe corpus_gs.exe compute_matrix.exe; do
    cp "$TMP/$probe.log" "$EVIDENCE/dxgi-3-$probe.log"
done
for dxvk_log in "$TMP"/dxvk/*; do
    [ -f "$dxvk_log" ] || continue
    cp "$dxvk_log" "$EVIDENCE/dxgi-3-$(basename "$dxvk_log")"
done

source_rev() {
    local dir="$1"
    if [ -d "$dir/.git" ]; then git -C "$dir" rev-parse HEAD; else echo unavailable; fi
}

wine_version="unavailable"
if [ -n "${WINE_BIN:-}" ] && [ -x "$WINE_BIN" ]; then
    wine_version="$("$WINE_BIN" --version 2>&1 | head -1)"
fi

{
    echo "# DXGI-3 window lifecycle evidence"
    echo
    echo "- Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "- DXGI source: Gcenx/DXVK-macOS, clean pinned base"
    echo "- DXVK base commit: $DXVK_COMMIT"
    echo "- DXVK bridge patch: $DXVK_PATCH"
    echo "- DXVK bridge patch SHA-256: $(shasum -a 256 "$DXVK_PATCH" | awk '{print $1}')"
    echo "- vkd3d-proton source revision: $(source_rev "$WS/sources/vkd3d-proton")"
    echo "- MoltenVK source revision: $(source_rev "$WS/sources/MoltenVK")"
    echo "- Wine runner: $RUNNER"
    echo "- Wine runtime: $wine_version"
    echo "- Host: $(sw_vers -productVersion)"
    echo "- Compiler: $("$WS/toolchain/llvm-mingw-20260616-ucrt-macos-universal/bin/x86_64-w64-mingw32-clang" --version | head -1)"
    echo
    echo "## Staged modules and hashes"
    echo
    echo '```text'
    for file in "$STAGE/dxgi.dll" "$STAGE/d3d12.dll" "$STAGE/d3d12core.dll" "$STAGE/libMoltenVK.dylib" "$STAGE/MoltenVK_icd.json"; do
        printf '%s\n' "$(realpath "$file")"
        file "$file"
        shasum -a 256 "$file"
    done
    echo '```'
    echo
    echo "## Lifecycle acceptance"
    echo
    echo '```text'
    grep -E "$summary_pattern" "$TMP/lifecycle-1.log"
    echo '```'
    echo
    echo "The probe creates a real Win32 window, selects the stage-1 adapter, renders the RGB triangle and clear color with GPU readback after each accepted lifecycle operation, and tracks PRESENT -> render-target -> copy-source -> PRESENT transitions. It exercises normal and zero/minimized resize, occlusion, fullscreen queries and fallback, destruction/recreation, invalid parameters, outstanding references, and 100 create/resize/destroy cycles. Shutdown is checked in GPU-idle, resources, swapchain, queue/fence, device, adapter, factory, window order."
    echo
    echo "DXGI-1 and DXGI-2 were rerun by the validator; their complete logs and the six-probe regression logs are adjacent to this document. This stage does not claim broad gameplay stability, resize/fullscreen support beyond the tested lane, or release readiness for later stages."
    echo
    echo "## Six-probe regression summary"
    echo
    echo '```text'
    cat "$TMP/regression-summary.txt"
    echo '```'
} > "$EVIDENCE/dxgi-3-lifecycle.md"

pass "evidence written to artifacts/evidence"
