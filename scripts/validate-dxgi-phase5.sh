#!/bin/bash
# DXGI-5 synchronization, pacing, resource-lifetime, and recovery gate.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh" >/dev/null
STAGE="${STAGE_DIR:-$WS/artifacts/stage-dxr}"
RUNNER="${WINE_RUNNER:-/tmp/run-probe.sh}"
PROBE="$STAGE/dxgi_sync_probe.exe"
DXVK_SRC="${DXVK_SRC:-$WS/sources/dxvk-macos}"
DXVK_COMMIT="${DXVK_COMMIT:-8f1e28deed3ad30802f7e1bdff428ec14e6e7817}"
EVIDENCE="$WS/artifacts/evidence"
LONG_FRAMES="${DXGI_SYNC_LONG_FRAMES:-100000}"
TMP="$(mktemp -d /tmp/dxgi-phase5.XXXXXX)"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

[ -x "$RUNNER" ] || fail "WINE_RUNNER is not executable: $RUNNER"
[ -f "$PROBE" ] || fail "DXGI-5 probe missing: $PROBE"
[ -d "$DXVK_SRC/.git" ] || fail "DXVK source is not a git checkout: $DXVK_SRC"
[ "$(git -C "$DXVK_SRC" rev-parse HEAD)" = "$DXVK_COMMIT" ] || fail "DXVK base commit mismatch"
git -C "$DXVK_SRC" diff --quiet || fail "DXVK source has uncommitted changes"
[ "$LONG_FRAMES" -ge 100000 ] || fail "DXGI_SYNC_LONG_FRAMES must be at least 100000"

for file in dxgi.dll d3d12.dll d3d12core.dll; do
    [ -f "$STAGE/$file" ] || fail "staged module missing: $file"
    file "$STAGE/$file" | grep -q 'PE32+.*x86-64' || fail "$file is not x86_64 PE32+"
done
[ -f "$STAGE/libMoltenVK.dylib" ] || fail "staged MoltenVK missing"
[ -f "$STAGE/MoltenVK_icd.json" ] || fail "staged MoltenVK ICD missing"

mkdir -p "$TMP/dxvk"
export DXVK_LOG_PATH="$TMP/dxvk"

check_probe_log() {
    local name="$1" log="$2" mode="$3"
    grep -q '^selected adapter:' "$log" || fail "adapter selection missing in $name"
    grep -q '^D3D12 adapter LUID:' "$log" || fail "D3D12 adapter identity missing in $name"
    grep -q ' (MATCH)$' "$log" || fail "adapter identity mismatch in $name"
    grep -q '^window/device/queue/swapchain: PASS$' "$log" || fail "window setup missing in $name"
    grep -q '^  queue Signal: hr=.* PASS$' "$log" || fail "queue signal missing in $name"
    grep -q '^  fence semantics: PASS$' "$log" || fail "fence semantics failed in $name"
    grep -q '^frame latency object:' "$log" || fail "frame-latency capability missing in $name"
    grep -q '^  frame run interval=0 frames=.*: PASS$' "$log" || fail "sync interval 0 failed in $name"
    grep -q '^  frame run interval=1 frames=.*: PASS$' "$log" || fail "sync interval 1 failed in $name"
    grep -Eq '^  release before GPU completion: (PASS|UNSUPPORTED)' "$log" || fail "resource lifetime classification missing in $name"
    grep -q '^  DXGI_PRESENT_TEST: hr=.* PASS$' "$log" || fail "occlusion/test Present failed in $name"
    grep -q '^  IDXGIDevice3::Trim:' "$log" || fail "Trim classification missing in $name"
    grep -q '^  invalid fence event: hr=' "$log" || fail "invalid event test missing in $name"
    grep -q '^  out-of-order fence signal:' "$log" || fail "out-of-order fence test missing in $name"
    grep -q '^  bounded unsignaled fence timeout: .*TIMEOUT PASS$' "$log" || fail "bounded timeout test missing in $name"
    grep -q '^GetDeviceRemovedReason(after invalid operation): hr=0x00000000 S_OK$' "$log" || fail "device reason after invalid operation missing in $name"
    grep -q '^GetDeviceRemovedReason(after timeout path): hr=0x00000000 S_OK$' "$log" || fail "device reason after timeout missing in $name"
    grep -q '^GetDeviceRemovedReason(after occlusion): hr=0x00000000 S_OK$' "$log" || fail "device reason after occlusion missing in $name"
    grep -q '^memory sample baseline' "$log" || fail "baseline memory sample missing in $name"
    grep -q '^memory sample final' "$log" || fail "final memory sample missing in $name"
    grep -q '^GetDeviceRemovedReason(normal): hr=0x00000000 PASS$' "$log" || fail "normal device reason failed in $name"
    grep -q "^frames-in-flight accepted mode=$mode$" "$log" || fail "frames-in-flight mode $mode missing in $name"
    grep -q '^DXGI-5 result: PASS (0 failures)$' "$log" || fail "DXGI-5 result failed in $name"
    if grep -Eqi 'Unhandled page fault|Assertion failed|deadlock|segmentation fault|MISMATCH|DXGI-5 result: FAIL' "$log"; then
        fail "failure signature in $name"
    fi
}

run_short() {
    local name="$1" mode="$2" raw log
    raw="$TMP/$name.raw"
    log="$TMP/$name.log"
    DXGI_SYNC_FRAMES=64 DXGI_SYNC_IN_FLIGHT="$mode" "$RUNNER" dxgi_sync_probe.exe >"$raw" 2>&1 \
        || { cat "$raw" >&2; fail "DXGI-5 short run $name exited nonzero"; }
    tr -d '\r' <"$raw" >"$log"
    check_probe_log "$name" "$log" "$mode"
}

run_short short-inflight-2 2
run_short short-inflight-3 3
run_short short-inflight-4 4
run_short short-repeat-3 3

short_signature() {
    local log="$1"
    grep -E '^(selected adapter:|window/device/queue/swapchain:|  queue Signal:|  cross-queue|  fence semantics:|  frame latency object:|  invalid fence event:|  out-of-order fence signal:|  bounded unsignaled fence timeout:|  release before GPU completion:|  DXGI_PRESENT_TEST:|  IDXGIDevice3::Trim:|GetDeviceRemovedReason\(after|  frame run interval=|GetDeviceRemovedReason\(normal\):|DXGI-5 result:)' "$log" \
        | sed -E 's/value=[0-9]+/value=N/g; s/frames=[0-9]+/frames=N/g; s/hr=0x[0-9a-f]+/hr=HRESULT/g'
}
cmp -s <(short_signature "$TMP/short-inflight-3.log") <(short_signature "$TMP/short-repeat-3.log") \
    || fail "short synchronization results were not repeatable"
pass "two repeatable short runs and frames-in-flight modes 2/3/4"

echo "=== DXGI-5 long stress: $LONG_FRAMES frames ==="
DXGI_SYNC_FRAMES="$LONG_FRAMES" DXGI_SYNC_IN_FLIGHT=3 "$RUNNER" dxgi_sync_probe.exe >"$TMP/long-stress.raw" 2>&1 \
    || { cat "$TMP/long-stress.raw" >&2; fail "DXGI-5 long stress exited nonzero"; }
tr -d '\r' <"$TMP/long-stress.raw" >"$TMP/long-stress.log"
check_probe_log long-stress "$TMP/long-stress.log" 3
[ "$(grep -c '^  resource/pipeline churn frame=' "$TMP/long-stress.log")" -ge 50 ] \
    || fail "long stress did not record sufficient resource churn"
[ "$(grep -c '^memory sample stress' "$TMP/long-stress.log")" -ge 5 ] \
    || fail "long stress did not record sufficient memory samples"
grep -q "^  frame run interval=0 frames=$LONG_FRAMES: PASS$" "$TMP/long-stress.log" \
    || fail "long stress frame count was not accepted"
pass "long synchronization stress ($LONG_FRAMES frames)"

# Re-run the complete earlier phase gates.  Phase 4 transitively reruns
# DXGI-1/2/3 and the six-probe regression suite while preserving their evidence.
bash "$WS/scripts/validate-dxgi-phase4.sh" >"$TMP/phase4-gate.log" 2>&1 \
    || { cat "$TMP/phase4-gate.log" >&2; fail "DXGI-1 through DXGI-4 or six-probe gate regressed"; }
pass "DXGI-1, DXGI-2, DXGI-3, DXGI-4, and six-probe gates"

source_rev() {
    local dir="$1"
    if [ -d "$dir/.git" ]; then git -C "$dir" rev-parse HEAD; else echo unavailable; fi
}

wine_version="unavailable"
if [ -n "${WINE_BIN:-}" ] && [ -x "$WINE_BIN" ]; then
    wine_version="$($WINE_BIN --version 2>&1 | head -1)"
fi
mkdir -p "$EVIDENCE"
for name in short-inflight-2 short-inflight-3 short-inflight-4 short-repeat-3; do
    cp "$TMP/$name.log" "$EVIDENCE/dxgi-5-$name.txt"
done
cp "$TMP/long-stress.log" "$EVIDENCE/dxgi-5-long-stress.txt"
cp "$TMP/phase4-gate.log" "$EVIDENCE/dxgi-5-phase4-gate.log"
for dxvk_log in "$TMP"/dxvk/*; do
    [ -f "$dxvk_log" ] || continue
    cp "$dxvk_log" "$EVIDENCE/dxgi-5-$(basename "$dxvk_log")"
done

{
    echo "# DXGI-5 synchronization, pacing, and recovery evidence"
    echo
    echo "- Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "- DXVK source commit: $DXVK_COMMIT"
    echo "- DXVK bridge patch SHA-256: $(shasum -a 256 "$WS/patches/dxvk-macos-d3d12-dxgi.patch" | awk '{print $1}')"
    echo "- vkd3d-proton source revision: $(source_rev "$WS/sources/vkd3d-proton")"
    echo "- MoltenVK source revision: $(source_rev "$WS/sources/MoltenVK")"
    echo "- Wine runner: $RUNNER"
    echo "- Wine runtime: $wine_version"
    echo "- Host: $(sw_vers -productVersion)"
    echo "- Compiler: $("$WS/toolchain/llvm-mingw-20260616-ucrt-macos-universal/bin/x86_64-w64-mingw32-clang" --version | head -1)"
    echo
    echo "## Staged modules and hashes"
    echo '```text'
    for file in "$STAGE/dxgi.dll" "$STAGE/d3d12.dll" "$STAGE/d3d12core.dll" "$STAGE/libMoltenVK.dylib" "$STAGE/MoltenVK_icd.json" "$PROBE"; do
        realpath "$file"; file "$file"; shasum -a 256 "$file"
    done
    echo '```'
    echo
    echo "## Long-run acceptance excerpt"
    echo '```text'
    grep -E '^(selected adapter:|D3D12 adapter LUID:|frames-in-flight|tearing support:|frame latency object:|window/device/queue|  queue Signal:|  cross-queue|  fence semantics:|  invalid fence|  out-of-order|  release before|  DXGI_PRESENT_TEST:|  IDXGIDevice3|  frame run interval=|memory sample|GetDeviceRemovedReason|memory bounded|  shutdown:|DXGI-5 result:)' "$TMP/long-stress.log"
    echo '```'
    echo
    echo "The waitable object, cross-queue path, Trim, and device-loss recovery are reported from actual HRESULTs. Unsupported backend behavior is not promoted to a pass. The run is synthetic synchronization evidence only and makes no broad gameplay-stability claim."
} > "$EVIDENCE/dxgi-5-synchronization.md"

pass "evidence written to artifacts/evidence"
