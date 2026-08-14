#!/bin/bash
# Build the custom MoltenVK universal dylib (x86_64 + arm64) using Xcode 27b4.
# Verified working 2026-08-14 (evidence full-selfbuild-run1: the built dylib runs
# the full D3D12 probe stack). Stages to $OUT.build.new; promotion requires probe evidence.
set -e
WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh"
SRC="$WS/sources/MoltenVK"
OUT="$WS/artifacts/build/moltenvk-vkmt"

[ -d "$SRC/MoltenVK" ] || { echo "MoltenVK source missing"; exit 1; }

cd "$SRC"
# 1) externals (SPIRV-Cross, SPIRV-Tools, Vulkan-Headers, glslang, ...) — builds XCFrameworks
./fetchDependencies --macos

# 2) dylib package
make macos

NEW="$OUT.build.new"
rm -rf "$NEW"
mkdir -p "$NEW"
cp -R Package/Release/MoltenVK/dynamic/dylib/macOS/* "$NEW/"
echo "== built (staged, not promoted) =="
file "$NEW/libMoltenVK.dylib"
shasum -a 256 "$NEW/libMoltenVK.dylib"
echo "promote with: cp $NEW/* $OUT/  (after probe evidence)"
