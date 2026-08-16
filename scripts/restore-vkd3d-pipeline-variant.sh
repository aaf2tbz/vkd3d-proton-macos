#!/bin/bash
# Restore the ORIGINAL d3d12_pipeline_state_create_pipeline_variant from the
# vkd3d git HEAD (the fork's committed version), replacing the version mangled
# by the debug-cleanup regex. My session's only intended change inside this
# function was the (now-removed) MESHPIPE debug FIXMEs, so the committed
# function is correct. The real MS/AS fix lives in
# vkd3d_pipeline_state_desc_from_d3d12_graphics_desc (a different function),
# which this script does NOT touch.
WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS/sources/vkd3d-proton"

# boundary markers of the mangled function in the working file
CUR="libs/vkd3d/state.c"
ORIG="$(git show HEAD:libs/vkd3d/state.c 2>/dev/null || echo EMPTY)"
if [ "$ORIG" = "EMPTY" ]; then echo "git show failed"; exit 1; fi

python3 - "$CUR" <<'PY'
import sys
cur = open(sys.argv[1]).read()
# extract the original function body between the signature and its closing
import subprocess
sg = subprocess.check_output(['git','show','HEAD:libs/vkd3d/state.c']).decode()
a = sg.find('VkPipeline d3d12_pipeline_state_create_pipeline_variant')
b = sg.find('VkPipeline d3d12_pipeline_state_create_pipeline_variant("early")')  # placeholder never matches
# find the end: next function definition after a
nxt = sg.find('static ', a)
# find the closing of this function: the next line at column 0 that starts a new definition
# Simpler: find the function start in current, and the next 'static'/'VkPipeline' definition
start_c = cur.find('VkPipeline d3d12_pipeline_state_create_pipeline_variant')
end_c = len(cur)
for marker in ['static VkPipeline d3d12_pipeline_state_', 'VkPipeline d3d12_pipeline_state_', 'static ' ]:
    pass
# Find the end of the original function: match braces
def find_func_end(src, sstart):
    depth = 0
    i = src.find('{', sstart)
    while i != -1 and i < len(src):
        if src[i] == '{': depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0: return i
        i += 1
    return -1
oe = find_func_end(sg, a)
ce = find_func_end(cur, start_c)
if oe == -1 or ce == -1:
    print("FAILED to delimit function"); sys.exit(1)
orig_fn = sg[a:oe+1]
print("orig_fn_bytes:", len(orig_fn))
cur2 = cur[:start_c] + orig_fn + cur[ce+1:]
open(sys.argv[1],'w').write(cur2)
print("restored. leftover loops:", cur2.count('for (unsigned _st'))
print("gfx-calls device:", cur2.count('vkCreateGraphicsPipelines(device->vk_device, vk_cache'))
print("ms/as extraction:", cur2.count('desc->ms = d3d12_desc->MS'))
PY
