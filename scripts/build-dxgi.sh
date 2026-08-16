#!/bin/bash
# Build the pinned DXVK-macOS DXGI provider for the Wine/Rosetta lane.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh" >/dev/null

DXVK_SRC="${DXVK_SRC:-$WS/sources/dxvk-macos}"
DXVK_COMMIT="${DXVK_COMMIT:-8f1e28deed3ad30802f7e1bdff428ec14e6e7817}"
DXVK_PATCH="${DXVK_PATCH:-$WS/patches/dxvk-macos-d3d12-dxgi.patch}"
DXVK_PHASE4_PATCH="${DXVK_PHASE4_PATCH:-$WS/patches/dxvk-macos-dxgi-phase4.patch}"
OUT="${DXVK_BUILD_DIR:-$WS/artifacts/build/dxvk-macos}"
CROSS="$WS/artifacts/dxvk-cross-x86_64.txt"
LLVM_MINGW="${LLVM_MINGW:?LLVM_MINGW is not set}"

[ -d "$DXVK_SRC/src/dxgi" ] || {
    echo "DXVK source missing: $DXVK_SRC" >&2
    echo "Clone Gcenx/DXVK-macOS there or set DXVK_SRC." >&2
    exit 1
}

actual_commit="$(git -C "$DXVK_SRC" rev-parse HEAD)"
[ "$actual_commit" = "$DXVK_COMMIT" ] || {
    echo "DXVK source commit mismatch: expected $DXVK_COMMIT, got $actual_commit" >&2
    exit 1
}
git -C "$DXVK_SRC" diff --quiet || {
    echo "DXVK source has uncommitted changes: $DXVK_SRC" >&2
    exit 1
}

patch_applied=0
cleanup_patch() {
    if [ "$patch_applied" -eq 1 ]; then
        git -C "$DXVK_SRC" reset --hard "$actual_commit" >/dev/null
        git -C "$DXVK_SRC" clean -fd -- src/dxgi/dxgi_d3d12.cpp src/dxgi/dxgi_d3d12.h >/dev/null
    fi
}
trap cleanup_patch EXIT

if [ -f "$DXVK_PATCH" ]; then
    git -C "$DXVK_SRC" apply --check "$DXVK_PATCH"
    git -C "$DXVK_SRC" apply "$DXVK_PATCH"
    patch_applied=1
else
    echo "DXVK patch missing: $DXVK_PATCH" >&2
    exit 1
fi

if [ -f "$DXVK_PHASE4_PATCH" ]; then
    git -C "$DXVK_SRC" apply --check "$DXVK_PHASE4_PATCH"
    git -C "$DXVK_SRC" apply "$DXVK_PHASE4_PATCH"
else
    echo "DXVK Phase-4 patch missing: $DXVK_PHASE4_PATCH" >&2
    exit 1
fi

cat > "$CROSS" <<EOF
[binaries]
c = '$LLVM_MINGW/bin/x86_64-w64-mingw32-clang'
cpp = '$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++'
ar = '$LLVM_MINGW/bin/llvm-ar'
strip = '$LLVM_MINGW/bin/llvm-strip'
windres = ['$LLVM_MINGW/bin/llvm-windres', '--target=pe-x86-64', '--preprocessor-arg=-I$LLVM_MINGW/generic-w64-mingw32/include']

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

if [ -f "$OUT/build.ninja" ]; then
    meson setup --reconfigure --cross-file "$CROSS" \
        --buildtype release --strip "$OUT" "$DXVK_SRC"
else
    meson setup --cross-file "$CROSS" \
        --buildtype release --strip "$OUT" "$DXVK_SRC"
fi
ninja -C "$OUT" src/dxgi/dxgi.dll

DEST="$WS/artifacts/build/dxvk-macos/x86_64-windows"
mkdir -p "$DEST"
cp "$OUT/src/dxgi/dxgi.dll" "$DEST/dxgi.dll"

echo "== built DXGI =="
file "$DEST/dxgi.dll"
shasum -a 256 "$DEST/dxgi.dll"
echo "source: $actual_commit"
echo "source patch: $DXVK_PATCH"
shasum -a 256 "$DXVK_PATCH"
echo "phase-4 patch: $DXVK_PHASE4_PATCH"
shasum -a 256 "$DXVK_PHASE4_PATCH"
