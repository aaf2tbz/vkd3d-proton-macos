#!/bin/bash
# DXGI-2 windowed swapchain, presentation, provenance, and regression gate.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh" >/dev/null
STAGE="${STAGE_DIR:-$WS/artifacts/stage-dxr}"
RUNNER="${WINE_RUNNER:-/tmp/run-probe.sh}"
PROBE="$STAGE/dxgi_present_probe.exe"
DXVK_SRC="${DXVK_SRC:-$WS/sources/dxvk-macos}"
DXVK_COMMIT="${DXVK_COMMIT:-8f1e28deed3ad30802f7e1bdff428ec14e6e7817}"
DXVK_PATCH="${DXVK_PATCH:-$WS/patches/dxvk-macos-d3d12-dxgi.patch}"
EVIDENCE="$WS/artifacts/evidence"
TMP="$(mktemp -d /tmp/dxgi-phase2.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[ -x "$RUNNER" ] || fail "WINE_RUNNER is not executable: $RUNNER"
[ -f "$PROBE" ] || fail "DXGI-2 probe missing: $PROBE"
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

run_present_probe() {
    local run="$1"
    local log="$TMP/present-$run.log"
    local raw="$TMP/present-$run.raw"
    "$RUNNER" dxgi_present_probe.exe >"$raw" 2>&1 || fail "DXGI-2 probe run $run exited nonzero"
    tr -d '\r' <"$raw" >"$log"
    grep -q 'DXGI-2 result: PASS' "$log" || fail "DXGI-2 probe run $run failed"
    grep -q '^D3D12 adapter LUID: .* (MATCH)$' "$log" || fail "DXGI/D3D12 adapter identity did not match on run $run"
    [ "$(grep -c '^  mode result: PASS$' "$log")" -eq 4 ] || fail "not all four presentation modes passed on run $run"
    [ "$(grep -c '^  frames: 1000 / 1000$' "$log")" -eq 4 ] || fail "not all modes completed 1000 frames on run $run"
    [ "$(grep -c '^  sync intervals exercised: 0 and 1; tearing flag on interval 0: ALLOW_TEARING$' "$log")" -eq 4 ] || fail "sync/tearing coverage missing on run $run"
    [ "$(grep -c '^  readback .*: PASS$' "$log")" -eq 4 ] || fail "pixel readback failed on run $run"
    [ "$(grep -Ec '^  Present\(DXGI_PRESENT_TEST\):.*PASS$' "$log")" -eq 2 ] || fail "Present test coverage missing on run $run"
    [ "$(grep -Ec '^  Present1\(DXGI_PRESENT_TEST\):.*PASS$' "$log")" -eq 2 ] || fail "Present1 test coverage missing on run $run"
    [ "$(grep -c 'GetFrameStatistics: hr=.*PASS/UNSUPPORTED$' "$log")" -eq 4 ] || fail "frame statistics coverage missing on run $run"
    [ "$(grep -c 'GetLastPresentCount: hr=.*count=1000 PASS/UNSUPPORTED$' "$log")" -eq 4 ] || fail "last-present-count coverage missing on run $run"
    grep -q 'tearing support: hr=0x00000000 supported=1' "$log" || fail "tearing support was not reported correctly on run $run"
    grep -q 'negative result: PASS (0 failures)' "$log" || fail "negative tests failed on run $run"
    if grep -Eq 'Unsupported device type|Could not find Vulkan physical device for DXGI adapter|DXGI-2 result: FAIL' "$log"; then
        fail "runtime reported a DXGI/D3D12 mapping failure on run $run"
    fi
}

run_present_probe 1
run_present_probe 2
cmp -s <(grep -E '^(tearing support:|=== |  create:|  Present|  readback|  frames:|  GetFrame|  mode result:|negative result:|DXGI-2 result:)' "$TMP/present-1.log") \
    <(grep -E '^(tearing support:|=== |  create:|  Present|  readback|  frames:|  GetFrame|  mode result:|negative result:|DXGI-2 result:)' "$TMP/present-2.log") \
    || fail "presentation result was not repeatable"
pass "two deterministic four-mode presentation runs"

for probe in cr_inner_probe.exe feedback_probe.exe mesh_probe.exe corpus.exe corpus_gs.exe compute_matrix.exe; do
    log="$TMP/$probe.log"
    raw="$TMP/$probe.raw"
    "$RUNNER" "$probe" >"$raw" 2>&1 || fail "regression failed: $probe"
    tr -d '\r' <"$raw" >"$log"
    grep -Eq 'RESULT:.*(WORKS|EXACTLY|PIXEL-EXACT)|WORKS' "$log" || fail "regression has no success marker: $probe"
    grep -Eq 'RESULT:.*FAIL' "$log" && fail "regression contains a failed result marker: $probe" || true
    grep -E 'RESULT:|WORKS' "$log" | tr -d '\r' | tail -1 | sed "s#^#$probe: #" >> "$TMP/regression-summary.txt"
done
pass "existing six-probe regression suite"

mkdir -p "$EVIDENCE"
cp "$TMP/present-1.log" "$EVIDENCE/dxgi-2-probe-run.txt"
cp "$TMP/present-2.log" "$EVIDENCE/dxgi-2-probe-repeat.txt"
cp "$TMP/regression-summary.txt" "$EVIDENCE/dxgi-2-regression-summary.txt"
for probe in cr_inner_probe.exe feedback_probe.exe mesh_probe.exe corpus.exe corpus_gs.exe compute_matrix.exe; do
    cp "$TMP/$probe.log" "$EVIDENCE/dxgi-2-$probe.log"
done
for dxvk_log in "$TMP"/dxvk/*; do
    [ -f "$dxvk_log" ] || continue
    cp "$dxvk_log" "$EVIDENCE/dxgi-2-$(basename "$dxvk_log")"
done

{
    echo "# DXGI-2 windowed presentation evidence"
    echo
    echo "- Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "- DXVK source: Gcenx/DXVK-macOS"
    echo "- DXVK base commit: $DXVK_COMMIT"
    echo "- DXVK bridge patch: $DXVK_PATCH"
    echo "- DXVK bridge patch SHA-256: $(shasum -a 256 "$DXVK_PATCH" | awk '{print $1}')"
    echo "- Wine runner: $RUNNER"
    echo "- Host: $(sw_vers -productVersion)"
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
    echo "## Presentation acceptance"
    echo
    echo '```text'
    grep -E '^(selected adapter:|D3D12 adapter LUID:|tearing support:|window/device/queue:|=== |  create:|  Present|  readback|  frames:|  sync intervals|  GetFrame|  mode result:|invalid |post-release|negative result:|DXGI-2 result:)' "$TMP/present-1.log"
    echo '```'
    echo
    echo "The full Wine/vkd3d/MoltenVK probe output is in dxgi-2-probe-run.txt and the repeatability run is in dxgi-2-probe-repeat.txt. DXVK logging was directed to the adjacent dxvk_* log when the provider emitted one."
    echo
    echo "## Existing regression summary"
    echo
    echo '```text'
    cat "$TMP/regression-summary.txt"
    echo '```'
    echo
    echo "This gate deliberately does not test resize, minimize, fullscreen, or broad gameplay stability; those belong to DXGI-3 and later."
} > "$EVIDENCE/dxgi-2-presentation.md"

pass "evidence written to artifacts/evidence"
