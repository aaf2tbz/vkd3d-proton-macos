#!/bin/bash
# dxc wrapper: runs the Windows dxc.exe under the configured Wine runtime.
# Usage: dxc.sh <dxc args...>  (e.g. -T cs_6_0 -E main shader.hlsl -Fo out.dxil)
WS="$(cd "$(dirname "$0")/.." && pwd)"
export WINEPREFIX="$WS/artifacts/prefix"
WINE_BIN="${WINE_BIN:-$(command -v wine 2>/dev/null || true)}"
DXC_EXE="${DXC_EXE:-$WS/artifacts/toolchain/dxc/bin/x64/dxc.exe}"
[ -n "$WINE_BIN" ] || { echo "WINE_BIN is not set; install Wine or export WINE_BIN" >&2; exit 1; }
[ -f "$DXC_EXE" ] || { echo "DXC_EXE not found: $DXC_EXE" >&2; exit 1; }
exec "$WINE_BIN" "$DXC_EXE" "$@"
