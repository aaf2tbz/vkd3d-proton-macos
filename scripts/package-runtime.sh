#!/bin/bash
# Package the verified runtime lanes for a GitHub release.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$WS/vkd3d-proton-macos.tar.zst}"
STAGE="${STAGE_DIR:-$WS/artifacts/stage-dxr}"
MVK="${MVK_DIR:-$WS/artifacts/build/moltenvk-vkmt}"
TMP="$(mktemp -d /tmp/vkd3d-runtime.XXXXXX)"
trap 'rm -rf "$TMP" "$OUT.tar"' EXIT
ROOT="$TMP/vkd3d-proton-macos"
mkdir -p "$ROOT"

for f in "$STAGE/d3d12.dll" "$STAGE/d3d12core.dll" \
         "$MVK/libMoltenVK.dylib" "$MVK/MoltenVK_icd.json"; do
	[ -f "$f" ] || { echo "missing runtime file: $f" >&2; exit 1; }
done

cp "$STAGE/d3d12.dll" "$ROOT/"
cp "$STAGE/d3d12core.dll" "$ROOT/"
cp "$MVK/libMoltenVK.dylib" "$ROOT/"
cp "$MVK/MoltenVK_icd.json" "$ROOT/"
cp "$WS/docs/release.md" "$ROOT/README.md"
(cd "$ROOT" && shasum -a 256 * > SHA256SUMS)

python3 - "$OUT" "$ROOT" <<'PY'
import os, sys, tarfile
out, root = sys.argv[1:]
with tarfile.open(out + '.tar', 'w', format=tarfile.PAX_FORMAT) as tar:
    for directory, dirs, files in os.walk(root):
        dirs.sort(); files.sort()
        for filename in files:
            path = os.path.join(directory, filename)
            info = tar.gettarinfo(path, arcname=os.path.relpath(path, os.path.dirname(root)))
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = 0
            with open(path, 'rb') as handle:
                tar.addfile(info, handle)
PY
zstd -19 -q -f "$OUT.tar" -o "$OUT"
rm -f "$OUT.tar"
zstd -t "$OUT"
echo "created $OUT"
