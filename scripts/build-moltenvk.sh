#!/bin/bash
# Build the custom MoltenVK universal dylib (x86_64 + arm64) using Xcode 27b4.
set -e
WS="$(cd "$(dirname "$0")/.." && pwd)"
source "$WS/scripts/env.sh"
SRC="$WS/sources/MoltenVK"
OUT="$WS/artifacts/build/moltenvk-vkmt"

[ -d "$SRC/MoltenVK" ] || { echo "MoltenVK source missing"; exit 1; }

cd "$SRC"
# Xcode build (this mirrors the repo's standard universal package build)
xcodebuild -project MoltenVKPackaging.xcodeproj -scheme "MoltenVK Package (macOS only)" \
    -configuration Release build 2>&1 | tail -20

mkdir -p "$OUT"
# NOTE: the canonical working artifact is the BUNDLE's custom dylib (sha 50e41de2…).
# This build stages into $OUT.build.new; promotion to $OUT requires probe evidence
# (per docs/02-build-toolchain.md baseline rule).
NEW="$OUT.build.new"
rm -rf "$NEW"
mkdir -p "$NEW"
cp -R Package/Release/MoltenVK/dynamic/dylib/macOS/* "$NEW/"
echo "== built (staged, not promoted) =="
file "$NEW/libMoltenVK.dylib"
cat "$NEW/MoltenVK_icd.json"
shasum -a 256 "$NEW/libMoltenVK.dylib"
