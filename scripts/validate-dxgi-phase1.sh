#!/bin/bash
# DXGI-1 adapter identity, provenance, repeatability, and regression gate.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh" >/dev/null
STAGE="${STAGE_DIR:-$WS/artifacts/stage-dxr}"
RUNNER="${WINE_RUNNER:-/tmp/run-probe.sh}"
PROBE="$STAGE/dxgi_probe.exe"
DXVK_SRC="${DXVK_SRC:-$WS/sources/dxvk-macos}"
DXVK_COMMIT="${DXVK_COMMIT:-8f1e28deed3ad30802f7e1bdff428ec14e6e7817}"
EVIDENCE="$WS/artifacts/evidence"
TMP="$(mktemp -d /tmp/dxgi-phase1.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[ -x "$RUNNER" ] || fail "WINE_RUNNER is not executable: $RUNNER"
[ -f "$PROBE" ] || fail "DXGI probe missing: $PROBE"
[ -d "$DXVK_SRC/.git" ] || fail "DXVK source is not a git checkout: $DXVK_SRC"
[ "$(git -C "$DXVK_SRC" rev-parse HEAD)" = "$DXVK_COMMIT" ] || fail "DXVK commit mismatch"
git -C "$DXVK_SRC" diff --quiet || fail "DXVK source has uncommitted changes"

for file in dxgi.dll d3d12.dll d3d12core.dll; do
    [ -f "$STAGE/$file" ] || fail "staged module missing: $file"
    file "$STAGE/$file" | grep -q 'PE32+.*x86-64' || fail "$file is not x86_64 PE32+"
done
[ -f "$STAGE/libMoltenVK.dylib" ] || fail "staged MoltenVK missing"

for run in $(seq 1 10); do
    log="$TMP/run-$run.log"
    "$RUNNER" dxgi_probe.exe >"$log" 2>&1 || fail "DXGI probe run $run exited nonzero"
    grep -q 'DXGI-1 result: PASS' "$log" || fail "DXGI probe run $run failed"
    grep -q 'DXGI/D3D12 LUID match: PASS' "$log" || fail "LUID mismatch on run $run"
    grep -q 'LoadLibrary(d3d12core): PASS' "$log" || fail "d3d12core did not load on run $run"
    grep -q 'D3D12GetInterface export: PASS' "$log" || fail "D3D12GetInterface missing on run $run"
    grep -q 'model: Apple M4' "$log" || fail "MoltenVK GPU identity missing on run $run"
    if grep -q 'Could not find Vulkan physical device for DXGI adapter' "$log"; then
        fail "DXGI-to-Vulkan adapter mapping failed on run $run"
    fi
done
first_luid="$(grep -m1 'selected LUID' "$TMP/run-1.log")"
for run in $(seq 2 10); do
    [ "$first_luid" = "$(grep -m1 'selected LUID' "$TMP/run-$run.log")" ] || fail "adapter LUID changed on run $run"
done
pass "10 deterministic adapter identity runs"

DXGI_PROBE_NEGATIVE=1 "$RUNNER" dxgi_probe.exe >"$TMP/negative.log" 2>&1 || fail "negative probe exited nonzero"
grep -q 'DXGI-1 negative result: PASS' "$TMP/negative.log" || fail "negative probe failed"
pass "negative adapter/feature-level tests"

for probe in cr_inner_probe.exe feedback_probe.exe mesh_probe.exe corpus.exe corpus_gs.exe compute_matrix.exe; do
    log="$TMP/$probe.log"
    "$RUNNER" "$probe" >"$log" 2>&1 || fail "regression failed: $probe"
    grep -Eq 'RESULT:|WORKS' "$log" || fail "regression has no success marker: $probe"
    grep -E 'RESULT:|WORKS' "$log" | tr -d '\r' | tail -1 | sed "s#^#$probe: #" >> "$TMP/regression-summary.txt"
done
pass "existing six-probe regression suite"

mkdir -p "$EVIDENCE"
cp "$TMP/run-1.log" "$EVIDENCE/dxgi-1-probe-run1.txt"
cp "$TMP/negative.log" "$EVIDENCE/dxgi-1-negative.txt"
cp "$TMP/regression-summary.txt" "$EVIDENCE/dxgi-1-regression-summary.txt"
{
    echo "# DXGI-1 adapter identity evidence"
    echo
    echo "- Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "- DXVK source: Gcenx/DXVK-macOS"
    echo "- DXVK commit: $DXVK_COMMIT"
    echo "- Wine runner: $RUNNER"
    echo "- Host: $(sw_vers -productVersion)"
    echo
    echo "## Staged artifacts"
    echo
    echo '```text'
    file "$STAGE/dxgi.dll" "$STAGE/d3d12.dll" "$STAGE/d3d12core.dll"
    shasum -a 256 "$STAGE/dxgi.dll" "$STAGE/d3d12.dll" "$STAGE/d3d12core.dll" "$STAGE/libMoltenVK.dylib"
    echo '```'
    echo
    echo "## Identity result"
    echo
    echo '```text'
    grep -E 'module |CreateDXGI|adapter [0-9]|name:|adapter LUID|selected|outputs:|D3D12|LUID match|LoadLibrary|export|DXGI-1 result' "$TMP/run-1.log"
    grep -E 'model:|vendorID:|deviceID:|Created VkDevice' "$TMP/run-1.log" | head -20
    echo '```'
    echo
    echo "Ten repeated runs produced the same selected adapter LUID:"
    echo
    echo '```text'
    for run in $(seq 1 10); do grep -m1 'selected LUID' "$TMP/run-$run.log"; done
    echo '```'
    echo
    echo "The full first-run and negative-test logs are stored beside this record."
    echo
    echo "## Regression summary"
    echo
    echo '```text'
    cat "$TMP/regression-summary.txt"
    echo '```'
} > "$EVIDENCE/dxgi-1-adapter-identity.md"
pass "evidence written to artifacts/evidence"
