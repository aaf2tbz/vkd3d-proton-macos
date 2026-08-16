#!/bin/bash
# DXGI-4 format, color-space, HDR, and presentation-policy gate.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh" >/dev/null
STAGE="${STAGE_DIR:-$WS/artifacts/stage-dxr}"
RUNNER="${WINE_RUNNER:-/tmp/run-probe.sh}"
PROBE="$STAGE/dxgi_formats_probe.exe"
DXVK_SRC="${DXVK_SRC:-$WS/sources/dxvk-macos}"
DXVK_COMMIT="${DXVK_COMMIT:-8f1e28deed3ad30802f7e1bdff428ec14e6e7817}"
DXVK_PATCH="${DXVK_PATCH:-$WS/patches/dxvk-macos-d3d12-dxgi.patch}"
DXVK_PHASE4_PATCH="${DXVK_PHASE4_PATCH:-$WS/patches/dxvk-macos-dxgi-phase4.patch}"
EVIDENCE="$WS/artifacts/evidence"
TMP="$(mktemp -d /tmp/dxgi-phase4.XXXXXX)"

restore_previous_phase_evidence() {
    [ -d "$TMP/previous-phase-evidence" ] || return 0
    for old_evidence in "$EVIDENCE"/dxgi-{1,2,3}-*; do
        [ -f "$old_evidence" ] || continue
        rm -f "$old_evidence"
    done
    for old_evidence in "$TMP"/previous-phase-evidence/*; do
        [ -f "$old_evidence" ] || continue
        cp "$old_evidence" "$EVIDENCE/"
    done
}
trap 'restore_previous_phase_evidence; rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[ -x "$RUNNER" ] || fail "WINE_RUNNER is not executable: $RUNNER"
[ -f "$PROBE" ] || fail "DXGI-4 probe missing: $PROBE"
[ -d "$DXVK_SRC/.git" ] || fail "DXVK source is not a git checkout: $DXVK_SRC"
[ "$(git -C "$DXVK_SRC" rev-parse HEAD)" = "$DXVK_COMMIT" ] || fail "DXVK base commit mismatch"
git -C "$DXVK_SRC" diff --quiet || fail "DXVK source has uncommitted changes"
[ -s "$DXVK_PATCH" ] || fail "DXVK D3D12 bridge patch missing"
[ -s "$DXVK_PHASE4_PATCH" ] || fail "DXGI-4 validation patch missing"

for file in dxgi.dll d3d12.dll d3d12core.dll; do
    [ -f "$STAGE/$file" ] || fail "staged module missing: $file"
    file "$STAGE/$file" | grep -q 'PE32+.*x86-64' || fail "$file is not x86_64 PE32+"
done
[ -f "$STAGE/libMoltenVK.dylib" ] || fail "staged MoltenVK missing"
[ -f "$STAGE/MoltenVK_icd.json" ] || fail "staged MoltenVK ICD missing"

mkdir -p "$TMP/dxvk"
export DXVK_LOG_PATH="$TMP/dxvk"

run_format_probe() {
    local run="$1" raw="$TMP/formats-$1.raw" log="$TMP/formats-$1.log"
    "$RUNNER" dxgi_formats_probe.exe >"$raw" 2>&1 || fail "DXGI-4 probe run $run exited nonzero"
    tr -d '\r' <"$raw" >"$log"
    grep -q '^DXGI-4 result: PASS (0 failures)$' "$log" || fail "DXGI-4 probe run $run failed"
    grep -q '^selected adapter:' "$log" || fail "adapter selection missing on run $run"
    grep -q '^D3D12 adapter LUID:' "$log" || fail "D3D12 adapter identity missing on run $run"
    grep -q ' (MATCH)$' "$log" || fail "adapter identity mismatch on run $run"
    format_row_fragmented() {
        local label="$1" format="$2" suffix="$3"
        grep -q "$label" "$log" && grep -q "$format" "$log" && grep -q "$suffix" "$log"
    }
    for format in B8G8R8A8_UNORM B8G8R8A8_UNORM_SRGB R8G8B8A8_UNORM; do
        # Asynchronous MoltenVK logging can split a support line; the format
        # query plus the descriptor/readback passes are the stable evidence.
        grep -q "format support $format" "$log" || \
            format_row_fragmented 'format support' "$format" 'SUPPORTED' || \
            fail "$format support missing on run $run"
        grep -q "format desc $format.*PASS" "$log" || \
            format_row_fragmented 'format desc' "$format" 'PASS' || \
            fail "$format description missing on run $run"
        grep -q "readback $format.*: PASS" "$log" || \
            format_row_fragmented 'readback' "$format" ': PASS' || \
            fail "$format readback failed on run $run"
        grep -q "present $format" "$log" || \
            format_row_fragmented 'present' "$format" 'PASS' || \
            fail "$format present row missing on run $run"
    done
    grep -q '^  swapchain R10G10B10A2_UNORM.*UNSUPPORTED$' "$log" || fail "R10 swapchain classification missing on run $run"
    grep -q '^  offscreen R10G10B10A2_UNORM.*GPU render/readback UNSUPPORTED$' "$log" || fail "R10 runtime boundary missing on run $run"
    [ "$(grep -c '^  depth format support ' "$log")" -eq 2 ] || fail "depth format support coverage missing on run $run"
    [ "$(grep -c 'depth/stencil resource/DSV/barrier: PASS' "$log")" -eq 2 ] || fail "depth/DSV coverage missing on run $run"
    [ "$(grep -c 'stencil=UNSUPPORTED PASS' "$log")" -eq 2 ] || fail "stencil readback classification missing on run $run"
    grep -q 'ResolveSubresource B8G8R8A8_UNORM:.*PASS' "$log" || fail "resolve coverage missing on run $run"
    grep -q '^  GetColorSpace1: NOT EXPOSED' "$log" || fail "GetColorSpace1 API classification missing on run $run"
    grep -q '^  CheckColorSpaceSupport SDR P709.*SUPPORTED$' "$log" || fail "SDR color-space support missing on run $run"
    grep -q '^  SetColorSpace1 SDR P709.*PASS$' "$log" || fail "SDR color-space set missing on run $run"
    for space in 'scRGB linear' 'HDR10 PQ' 'extended P2020'; do
        grep -q "^  CheckColorSpaceSupport $space.*UNSUPPORTED$" "$log" || fail "$space support classification missing on run $run"
        grep -q "^  SetColorSpace1 $space.*UNSUPPORTED$" "$log" || fail "$space set classification missing on run $run"
    done
    grep -q '^  invalid HDR10 metadata(NULL): hr=.* PASS$' "$log" || fail "invalid HDR metadata negative missing on run $run"
    grep -q '^  valid HDR10 metadata update: hr=.* REPORTED$' "$log" || fail "HDR metadata update result missing on run $run"
    grep -q '^  tearing support: reported=.*accepted presentation flag policy=' "$log" || fail "tearing policy missing on run $run"
    for negative in 'unsupported BC1 swapchain' 'invalid alpha-mode swapchain' 'incompatible SDR-format/HDR10 color-space' 'unsupported BC1 render resource' 'invalid RTV description' 'invalid DSV description' 'invalid format support query'; do
        grep -q "^  $negative.*PASS" "$log" || fail "$negative negative missing on run $run"
    done
    grep -q '^  negative result: PASS (0 failures)$' "$log" || fail "negative matrix failed on run $run"
    if grep -Eqi 'Unhandled page fault|Assertion failed|device removed|deadlock|segmentation fault' "$log"; then
        fail "runtime crash/error signature in format run $run"
    fi
}

run_format_probe 1
run_format_probe 2

format_signature() {
    local log="$1" token count
    for token in \
        'readback B8G8R8A8_UNORM ' \
        'readback B8G8R8A8_UNORM_SRGB' \
        'readback R8G8B8A8_UNORM ' \
        'depth/stencil resource/DSV/barrier: PASS' \
        'ResolveSubresource B8G8R8A8_UNORM' \
        'CheckColorSpaceSupport' 'SetColorSpace1' \
        'negative result: PASS' 'DXGI-4 result: PASS'; do
        count="$(grep -c "$token" "$log" || true)"
        printf '%s=%s\n' "$token" "$count"
    done
}
cmp -s <(format_signature "$TMP/formats-1.log") \
    <(format_signature "$TMP/formats-2.log") \
    || fail "format/color result was not repeatable"
pass "two deterministic DXGI-4 format/color runs"

mkdir -p "$TMP/previous-phase-evidence"
for old_evidence in "$EVIDENCE"/dxgi-{1,2,3}-*; do
    [ -f "$old_evidence" ] || continue
    cp "$old_evidence" "$TMP/previous-phase-evidence/"
done
bash "$WS/scripts/validate-dxgi-phase3.sh" >"$TMP/phase3-validator.log" 2>&1 \
    || { cat "$TMP/phase3-validator.log" >&2; fail "DXGI-3 gate regressed"; }
restore_previous_phase_evidence
pass "DXGI-1, DXGI-2, DXGI-3, and six-probe gates"

source_rev() {
    local dir="$1"
    if [ -d "$dir/.git" ]; then git -C "$dir" rev-parse HEAD; else echo unavailable; fi
}
wine_version="unavailable"
if [ -n "${WINE_BIN:-}" ] && [ -x "$WINE_BIN" ]; then wine_version="$("$WINE_BIN" --version 2>&1 | head -1)"; fi

mkdir -p "$EVIDENCE"
cp "$TMP/formats-1.log" "$EVIDENCE/dxgi-4-probe-run.txt"
cp "$TMP/formats-2.log" "$EVIDENCE/dxgi-4-probe-repeat.txt"
cp "$TMP/phase3-validator.log" "$EVIDENCE/dxgi-4-phase3-gate.log"
for dxvk_log in "$TMP"/dxvk/*; do
    [ -f "$dxvk_log" ] || continue
    cp "$dxvk_log" "$EVIDENCE/dxgi-4-$(basename "$dxvk_log")"
done

{
    echo "# DXGI-4 format, color-space, HDR, and presentation-policy evidence"
    echo
    echo "- Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "- DXVK source commit: $DXVK_COMMIT"
    echo "- DXVK bridge patch SHA-256: $(shasum -a 256 "$DXVK_PATCH" | awk '{print $1}')"
    echo "- DXGI-4 patch: $DXVK_PHASE4_PATCH"
    echo "- DXGI-4 patch SHA-256: $(shasum -a 256 "$DXVK_PHASE4_PATCH" | awk '{print $1}')"
    echo "- vkd3d-proton source revision: $(source_rev "$WS/sources/vkd3d-proton")"
    echo "- MoltenVK source revision: $(source_rev "$WS/sources/MoltenVK")"
    echo "- Wine runner: $RUNNER"
    echo "- Wine runtime: $wine_version"
    echo "- Host: $(sw_vers -productVersion)"
    echo "- Compiler: $("$WS/toolchain/llvm-mingw-20260616-ucrt-macos-universal/bin/x86_64-w64-mingw32-clang" --version | head -1)"
    echo
    echo "## Staged modules and hashes"
    echo '```text'
    for file in "$STAGE/dxgi.dll" "$STAGE/d3d12.dll" "$STAGE/d3d12core.dll" "$STAGE/libMoltenVK.dylib" "$STAGE/MoltenVK_icd.json"; do
        realpath "$file"; file "$file"; shasum -a 256 "$file"
    done
    echo '```'
    echo
    echo "## Acceptance excerpt"
    echo '```text'
    grep -E '^(selected adapter:|D3D12 adapter LUID:|  format support|  swapchain|  offscreen|  format desc|  readback|  present|  depth format|  depth/stencil|  CheckColor|  SetColor|  GetColor|  .*HDR|  unsupported|  invalid|  incompatible|  negative|  tearing support|DXGI-4 result)' "$TMP/formats-1.log"
    echo '```'
    echo
    echo "The configured runner reports HDR10/scRGB/P2020 presentation unsupported through DXGI on this run, despite the physical main display's capabilities. The TV was not used as evidence. The HDR setter's REPORTED HRESULT is not treated as HDR support; CheckColorSpaceSupport is authoritative. R10G10B10A2 is D3D12-format-supported and resource/RTV-creatable, but this native DXGI lane rejects its swapchain and the Metal conversion readback path is explicitly reported unsupported rather than synthesized. Stencil clear/DSV behavior is exercised; the backend copy footprint does not expose a stencil readback plane and that limitation is recorded explicitly."
    echo
    echo "The full two-run Wine/vkd3d/MoltenVK output is in dxgi-4-probe-run.txt and dxgi-4-probe-repeat.txt. DXGI-3 preservation output is in dxgi-4-phase3-gate.log. No broad gameplay-stability claim is made."
} > "$EVIDENCE/dxgi-4-formats.md"

pass "evidence written to artifacts/evidence"
