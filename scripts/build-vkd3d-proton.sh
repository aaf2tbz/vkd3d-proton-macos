#!/bin/bash
# Build the custom vkd3d-proton PE pair (d3d12.dll forwarder + d3d12core.dll impl)
# with llvm-mingw + ninja + meson, targeting x86_64-windows.
set -e
WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh"
SRC="$WS/sources/vkd3d-proton"
OUT="$WS/artifacts/build/vkd3d-proton-build"

[ -d "$SRC/libs/vkd3d" ] || { echo "vkd3d-proton source missing"; exit 1; }

cat > "$WS/artifacts/vkd3d-cross-x86_64.txt" <<'EOF'
[binaries]
c = 'x86_64-w64-mingw32-clang'
cpp = 'x86_64-w64-mingw32-clang++'
ar = 'llvm-ar'
strip = 'llvm-strip'
windres = 'llvm-windres'
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
[properties]
needs_exe_wrapper = true
EOF

cd "$SRC"
meson setup --cross-file "$WS/artifacts/vkd3d-cross-x86_64.txt" \
    --buildtype release --strip \
    "$OUT" . || { echo "meson setup failed (check meson_options.txt for current flags)"; exit 1; }
ninja -C "$OUT"

mkdir -p "$WS/artifacts/build/vkd3d-proton/x86_64-windows"
# locate the built DLLs wherever meson placed them
find "$OUT" -name 'd3d12.dll'   -exec cp {} "$WS/artifacts/build/vkd3d-proton/x86_64-windows/" \; -print
find "$OUT" -name 'd3d12core.dll' -exec cp {} "$WS/artifacts/build/vkd3d-proton/x86_64-windows/" \; -print

echo "== built =="
file "$WS"/artifacts/build/vkd3d-proton/x86_64-windows/*.dll
shasum -a 256 "$WS"/artifacts/build/vkd3d-proton/x86_64-windows/*.dll
