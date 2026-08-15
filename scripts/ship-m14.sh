#!/bin/bash
# M14 ship: assemble the fully-working implementation tarball.
# Usage: bash scripts/ship-m14.sh
# Produces artifacts/VKD3D-Proton-MacOS-<date>.tar.zst with:
#   - the workspace's committed state (docs, scripts, evidence)
#   - the four fork source trees with their git histories (build/ excluded)
#   - the built artifacts (d3d12 pair, libMoltenVK, ICD)
#   - the repackaged metalsharp-graphics-dll bundle
#   - BUILD.md (toolchain + build steps + launch env)
set -e
WS="$(cd "$(dirname "$0")/.." && pwd)"
DATE="$(date +%Y-%m-%d)"
STAGE="$(mktemp -d /tmp/vkd3d-m14-ship.XXXXXX)"
OUT="$WS/artifacts/VKD3D-Proton-MacOS-$DATE.tar.zst"

mkdir -p "$STAGE/VKD3D-Proton-MacOS-$DATE"

# 1. workspace committed state
(cd "$WS" && git archive HEAD | tar -x -C "$STAGE/VKD3D-Proton-MacOS-$DATE")
# 2. fork trees with git history (exclude build dirs)
for f in MoltenVK vkd3d-proton SPIRV-Cross MetalSharp; do
  SRC="$WS/sources/$f"
  DST="$STAGE/VKD3D-Proton-MacOS-$DATE/sources/$f"
  if [ -d "$SRC/.git" ]; then
    mkdir -p "$DST"
    cp -R "$SRC/.git" "$DST/.git"
    # working tree minus the heavy build dirs
    (cd "$SRC" && git ls-files -z | rsync -a --from0 --files-from=- ./ "$DST/" 2>/dev/null || \
     (cd "$SRC" && git ls-files -z | tar --null -T - -c | tar -x -C "$DST"))
    # symlinked submodules (SPIRV-Cross inside MoltenVK) - copy the real tree
    if [ "$f" = "MoltenVK" ]; then
      if [ -d "$SRC/External/SPIRV-Cross/.git" ]; then
        mkdir -p "$DST/External/SPIRV-Cross"
        cp -R "$SRC/External/SPIRV-Cross/.git" "$DST/External/SPIRV-Cross/.git"
        (cd "$SRC/External/SPIRV-Cross" && git ls-files -z | tar --null -T - -c | tar -x -C "$DST/External/SPIRV-Cross")
      fi
    fi
  else
    mkdir -p "$DST"
    tar -C "$SRC" --exclude='build' --exclude='.git' -cf - . | tar -C "$DST" -xf -
  fi
  echo "  staged $f"
done
# 3. built artifacts
mkdir -p "$STAGE/VKD3D-Proton-MacOS-$DATE/artifacts/bin"
cp "$WS/artifacts/build/vkd3d-proton/x86_64-windows/d3d12.dll" "$STAGE/VKD3D-Proton-MacOS-$DATE/artifacts/bin/"
cp "$WS/artifacts/build/vkd3d-proton/x86_64-windows/d3d12core.dll" "$STAGE/VKD3D-Proton-MacOS-$DATE/artifacts/bin/"
cp "$WS/sources/MoltenVK/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$STAGE/VKD3D-Proton-MacOS-$DATE/artifacts/bin/"
cp "$WS/artifacts/build/moltenvk-vkmt/MoltenVK_icd.json" "$STAGE/VKD3D-Proton-MacOS-$DATE/artifacts/bin/"
# 4. the repackaged bundle
cp "$WS/artifacts/metalsharp-graphics-dll-m12.tar.zst" "$STAGE/VKD3D-Proton-MacOS-$DATE/artifacts/"
# 5. BUILD.md
cat > "$STAGE/VKD3D-Proton-MacOS-$DATE/BUILD.md" <<'BUILD'
# Build & run (M14 ship)

## Toolchain
- llvm-mingw-20260616-ucrt-macos-universal (clang 22.1.8) - exact shipped compiler
- ninja, meson, Xcode (beta) for MoltenVK
- See docs/02-build-toolchain.md

## Fork builds (sources/<fork>)
- vkd3d-proton: `bash scripts/build-vkd3d-proton.sh` (produces d3d12.dll + d3d12core.dll)
- MoltenVK: `bash scripts/build-moltenvk.sh` (produces libMoltenVK.dylib)
- The built artifacts are also committed under artifacts/bin/

## Bundle
- artifacts/metalsharp-graphics-dll-m12.tar.zst: the repackaged graphics bundle
  with the DXR-12_2 vkd3d lane + the MoltenVK lane (hash-pinned in
  MetalSharp's installer.rs: d3d12.dll 0fc39950..., d3d12core.dll 1659e641...,
  libMoltenVK.dylib 2e25de79...)

## Launch env (wine)
```
WINEPREFIX=<ws>/artifacts/prefix
WINEDLLOVERRIDES="d3d12,d3d12core,dxgi=n,b"
DYLD_LIBRARY_PATH=<dir-with-libMoltenVK>
DYLD_FALLBACK_LIBRARY_PATH=<dir-with-libMoltenVK>:<ws>:<wine>/lib:<wine>/lib/wine/x86_64-unix
VK_ICD_FILENAMES=<ws>/MoltenVK_icd.json
MVK_PRESENT_MODE=1
VKMT_ALLOW_NON_SINGLE_TEXEL_ALIGNMENT=1
```

## Verification
- `scripts/flprobe.c` -> all rungs create (11_0..12_2, CORE_1_0), max=12_2
- `scripts/probes/core10/compute_matrix.c` -> CORE compute matrix (readback 42.0)
- `scripts/probes/vk-as/vk-as-probe.c` -> inline ray query (minD=5.000 exact)
- Evidence: artifacts/evidence/
BUILD
echo "  wrote BUILD.md"

# 6. normalize (uid/gid 0, mtime 0, deterministic order) via python tarfile -> tar
python3 - "$OUT" "$STAGE" "VKD3D-Proton-MacOS-$DATE" <<'PY'
import sys, tarfile, os
out, stage, name = sys.argv[1], sys.argv[2], sys.argv[3]
root = os.path.join(stage, name)
paths = []
for dirpath, dirnames, filenames in os.walk(root):
    for d in dirnames:
        paths.append(os.path.join(dirpath, d))
    for f in filenames:
        paths.append(os.path.join(dirpath, f))
paths.sort()
tar_path = out + ".tar"
with tarfile.open(tar_path, "w", format=tarfile.PAX_FORMAT) as tar:
    for p in paths:
        arc = os.path.relpath(p, stage)
        info = tar.gettarinfo(p, arcname=arc)
        info.uid = 0; info.gid = 0; info.uname = ""; info.gname = ""
        info.mtime = 0
        if info.isreg():
            with open(p, "rb") as f:
                tar.addfile(info, f)
        else:
            tar.addfile(info)
print("packed", len(paths), "entries")
PY
zstd -19 -q -f "$OUT.tar" -o "$OUT"
rm -f "$OUT.tar"

echo "shipped: $OUT"
ls -la "$OUT"
rm -rf "$STAGE"
