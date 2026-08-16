#!/bin/bash
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${DXGI_PRESENT_SHADER_DIR:-$WS/artifacts/stage-dxr/dxgi-present}"
mkdir -p "$OUT"

"$WS/scripts/dxc.sh" -T vs_6_0 -E vs \
    "$WS/scripts/probes/dxgi-present/triangle.hlsl" \
    -Fo "$OUT/triangle_vs.dxil"
"$WS/scripts/dxc.sh" -T ps_6_0 -E ps \
    "$WS/scripts/probes/dxgi-present/triangle.hlsl" \
    -Fo "$OUT/triangle_ps.dxil"

file "$OUT/triangle_vs.dxil" "$OUT/triangle_ps.dxil"
