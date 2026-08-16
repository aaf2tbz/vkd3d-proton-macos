#!/usr/bin/env bash
# Compile every checked-in Metal source with Apple's Metal compiler.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
METAL="$(xcrun --find metal)"
OUT="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/vkd3d-metal-air"

rm -rf "$OUT"
mkdir -p "$OUT"
"$METAL" --version

count=0
while IFS= read -r source; do
    output="$OUT/$(printf '%s' "$source" | tr '/.' '__').air"
    echo "metal $source"
    "$METAL" -c "$ROOT/$source" -o "$output"
    count=$((count + 1))
done < <(cd "$ROOT" && git ls-files '*.metal' | sort)

[ "$count" -gt 0 ] || { echo "no Metal sources were found" >&2; exit 1; }
echo "Metal source check: PASS ($count shaders)"
