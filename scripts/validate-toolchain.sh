#!/bin/bash
# One-shot verification of every tool the workspace promises.
set -u
WS="$(cd "$(dirname "$0")/.." && pwd)"
pass=0; fail=0
ok()  { echo "  PASS  $1"; pass=$((pass+1)); }
bad() { echo "  FAIL  $1"; fail=$((fail+1)); }

echo "== llvm-mingw =="
LM="$WS/toolchain/llvm-mingw-20260616-ucrt-macos-universal"
[ -x "$LM/bin/x86_64-w64-mingw32-clang" ] && ok "x86_64-w64-mingw32-clang" || bad "x86_64-w64-mingw32-clang"
V=$("$LM/bin/x86_64-w64-mingw32-clang" --version 2>/dev/null | head -1)
echo "      $V"
echo "$V" | grep -q '22.1.8' && ok "clang 22.1.8 (shipped-build compiler match)" || bad "clang 22.1.8 expected"

echo "== build tools =="
for t in ninja meson cmake; do
  command -v $t >/dev/null && ok "$t ($(command -v $t))" || bad "$t missing"
done

echo "== Xcode 27 beta 4 =="
XB="/Users/averyfelts/Downloads/Xcode-beta.app/Contents/Developer"
[ -d "$XB" ] && ok "Xcode-beta.app present" || bad "Xcode-beta.app missing"
DV=$(DEVELOPER_DIR="$XB" xcodebuild -version 2>/dev/null | head -2 | tr '\n' ' ')
echo "      $DV"
echo "$DV" | grep -q 'Xcode 27' && ok "Xcode 27" || bad "Xcode 27 expected"

echo "== metal / metallib / MSL =="
MT=$(DEVELOPER_DIR="$XB" xcrun -sdk macosx metal --version 2>&1 | head -1)
echo "      $MT"
command -v xcrun >/dev/null && ok "xcrun" || bad "xcrun"
DEVELOPER_DIR="$XB" xcrun -sdk macosx -f metallib >/dev/null 2>&1 && ok "metallib" || bad "metallib"
DEVELOPER_DIR="$XB" xcrun -sdk macosx -f metal-tt >/dev/null 2>&1 && ok "metal-tt" || bad "metal-tt"
# smoke: compile a trivial MSL shader
TMP=$(mktemp -d)
cat > "$TMP/t.metal" <<'EOF'
#include <metal_stdlib>
using namespace metal;
kernel void k(device float* o [[buffer(0)]]) { o[0] = 1.0f; }
EOF
if DEVELOPER_DIR="$XB" xcrun -sdk macosx metal -c "$TMP/t.metal" -o "$TMP/t.air" 2>/dev/null; then
  ok "MSL compile smoke (metal)"
  if DEVELOPER_DIR="$XB" xcrun -sdk macosx metallib "$TMP/t.air" -o "$TMP/t.metallib" 2>/dev/null; then
    ok "metallib smoke"
  else bad "metallib smoke"; fi
else
  bad "MSL compile smoke"
fi
rm -rf "$TMP"

echo "== CLT beta =="
command -v clang >/dev/null && ok "clang ($(clang --version 2>/dev/null | head -1))" || bad "clang"
xcode-select -p 2>/dev/null | grep -q CommandLineTools && ok "xcode-select → CLT" || bad "xcode-select"

echo "== Wine runtime =="
WINE_BIN="${WINE_BIN:-$(command -v wine 2>/dev/null || true)}"
[ -n "$WINE_BIN" ] && ok "wine runtime present ($WINE_BIN)" || bad "wine runtime missing (set WINE_BIN)"
[ -n "$WINE_BIN" ] && "$WINE_BIN" --version 2>/dev/null | head -1 | sed 's/^/      /' || true

echo "== sources (fresh clones) =="
for s in vkd3d-proton MoltenVK SPIRV-Cross; do
  if git -C "$WS/sources/$s" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    C=$(git -C "$WS/sources/$s" log -1 --format='%h %s' 2>/dev/null | cut -c1-60)
    ok "$s ($C)"
  else
    bad "$s (clone incomplete)"
  fi
done

echo
echo "RESULT: $pass passed, $fail failed"
[ $fail -eq 0 ]
