#!/bin/bash
# dxc wrapper: runs the Windows dxc.exe under the workspace wine runtime.
# Usage: dxc.sh <dxc args...>  (e.g. -T cs_6_0 -E main shader.hlsl -Fo out.dxil)
WS="$(cd "$(dirname "$0")/.." && pwd)"
export WINEPREFIX="$WS/artifacts/prefix"
exec /Users/averyfelts/.metalsharp/runtime/wine/bin/wine "$WS/artifacts/toolchain/dxc/bin/x64/dxc.exe" "$@"
