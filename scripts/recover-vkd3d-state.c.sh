#!/bin/bash
# Repair the vkd3d state.c damaged by the debug-cleanup regex (2026-08-16).
# The cleanup ate the `vr = VK_CALL(vkCreateGraphicsPipelines(...))` line and the
# `if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))` line at BOTH call sites,
# leaving an orphaned `for (unsigned _st ...) {` debug-loop header.
cd "$(dirname "$0")/.."
python3 - <<'PY'
import re
p = 'sources/vkd3d-proton/libs/vkd3d/state.c'
s = open(p).read()
before = s.count('for (unsigned _st')

# Replace the orphaned debug-loop header that follows the prepend with the
# restored real call, at both sites (any indentation).
# Pattern: `vk_prepend_struct(&pipeline_desc, &flags2);` then blank line then the orphan loop.
pat = re.compile(
    r'(\t+if \(flags2\.flags\)\n'
    r'\t+vk_prepend_struct\(&pipeline_desc, &flags2\);\n\n'
    r')(?:[ \t]+for \(unsigned _st = 0; _st < pipeline_desc\.stageCount; _st\+\+\)\n'
    r'[ \t]+\{\n)([ \t]+const char \*kind;\n)',
    re.M)
repl = (
    r'\1'
    r'\tvr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device, vk_cache, 1, &pipeline_desc, NULL, &vk_pipeline));\n'
    r'\n'
    r'\tif (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))\n'
    r'\t{\n'
    r'\2')
new, n = pat.subn(repl, s)
if n != 2:
    # fallback for the library-link site which used state->device->vk_device
    pat2 = re.compile(
        r'(\t+if \(flags2\.flags\)\n'
        r'\t+vk_prepend_struct\(&pipeline_desc, &flags2\);\n\n'
        r')(?:[ \t]+for \(unsigned _st = 0; _st < pipeline_desc\.stageCount; _st\+\+\)\n'
        r'[ \t]+\{\n)([ \t]+const char \*kind;\n)',
        re.M)
    new2, n2 = pat2.subn(
        lambda m: (m.group(1)
            + '\tvr = VK_CALL(vkCreateGraphicsPipelines(state->device->vk_device, vk_cache, 1, &pipeline_desc, NULL, &vk_pipeline));\n\n'
            + '\tif (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))\n\t{\n' + m.group(2)),
        s)
    n = n2
    new = new2
open(p, 'w').write(new)
after = new.count('for (unsigned _st')
print(f"replaced={n} leftover_loops_before={before} leftover_loops_after={after}")
print(f"restored_calls={new.count('vr = VK_CALL(vkCreateGraphicsPipelines')}")
if n == 0:
    print("WARNING: no site matched — inspect state.c manually")
PY
