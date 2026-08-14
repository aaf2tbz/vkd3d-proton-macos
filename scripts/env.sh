#!/bin/bash
# VKD3D-Proton-MacOS workspace environment.
# Usage: source scripts/env.sh
WS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export WS
export LLVM_MINGW="$WS/toolchain/llvm-mingw-20260616-ucrt-macos-universal"
export PATH="$LLVM_MINGW/bin:/opt/homebrew/bin:$PATH"
# Xcode 27 beta 4 (MSL/metallib/metal toolchain)
export DEVELOPER_DIR="/Users/averyfelts/Downloads/Xcode-beta.app/Contents/Developer"
# Installed MetalSharp Wine 11.5 runtime (launch/probe target only — never modified until M14)
export MS_RUNTIME="/Users/averyfelts/.metalsharp/runtime/wine"
export MS_WINE="$MS_RUNTIME/bin/wine"
# Bundle archive the final PR upgrades (M14)
export MS_GRAPHICS_TAR="/Users/averyfelts/Desktop/metalsharp-graphics-dll-clean.tar.zst"
# Build outputs
export VKD3D_BUILD="$WS/artifacts/build/vkd3d-proton-build"
export MVK_PACKAGE="$WS/sources/MoltenVK/Package/Release/MoltenVK/dynamic/dylib/macOS"
echo "[env] WS=$WS"
echo "[env] llvm-mingw clang: $($LLVM_MINGW/bin/x86_64-w64-mingw32-clang --version 2>/dev/null | head -1)"
echo "[env] DEVELOPER_DIR=$DEVELOPER_DIR"
