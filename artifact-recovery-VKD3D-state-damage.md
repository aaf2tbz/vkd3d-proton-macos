# RECOVERY NOTES — vkd3d state.c damaged by debug-cleanup regex (2026-08-16)

The python regex used to strip the MESHPIPE debug markers from
sources/vkd3d-proton/libs/vkd3d/state.c over-removed ~60 lines after each
'MESHPIPE7: main call' FIXME, deleting the `vr = VK_CALL(vkCreateGraphicsPipelines(...))`
line AND the `if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))` line at BOTH
vkCreateGraphicsPipelines call sites (the library-link path ~line 6786 and the direct
graphics path ~line 7026/7076).

The mangled text at each site looks like:
```c
    if (flags2.flags)
        vk_prepend_struct(&pipeline_desc, &flags2);

        for (unsigned _st = 0; _st < pipeline_desc.stageCount; _st++)
            {
        const char *kind;
```
(the orphaned `for (unsigned _st...` debug loop header + `{` remain, and the real
vkCreateGraphicsPipelines call + the timeline-trace `if` were eaten).

CORRECT text (site 1, the direct path, 4-space base indent):
```c
    if (flags2.flags)
        vk_prepend_struct(&pipeline_desc, &flags2);

    vr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device, vk_cache, 1, &pipeline_desc, NULL, &vk_pipeline));

    if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
    {
        const char *kind;
```
There are TWO such sites (the first ~line 6786 area used `state->device->vk_device`, the
later two used `device->vk_device`). Each had a `MESHPIPE7: main call` FIXME + the `for (unsigned _st...)`
debug loop. Fix ALL broken `for (unsigned _st` remnants by restoring the vkCreateGraphicsPipelines
call + the `if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))` line before `const char *kind;`.

SYMPTOM: after the bad rebuild, all three graphics-PSO probes (cr_inner_probe, feedback_probe,
mesh_probe) fail with wine exit 5 / crash in d3d12core, while corpus.exe and compute_matrix.exe
(no graphics PSO) still pass. This confirms the graphics-pipeline vkd3d path is broken by the
missing vkCreateGraphicsPipelines call.

NOTE: There was ALSO a transient tool-harness outage that produced empty tool outputs; the last
action (a python restore replacing text) may or may not have written. VERIFY FIRST: count of
`for (unsigned _st` should be 0, and count of `vkCreateGraphicsPipelines(device->vk_device, vk_cache`
should be 2 (the two direct-path sites) after the restore. If loops>0, re-apply the fix.

Build after fixing: cd /Volumes/AverySSD/VKD3D-Proton-MacOS && source scripts/env.sh && \
  ninja -C artifacts/build/vkd3d-proton-build && \
  cp artifacts/build/vkd3d-proton-build/libs/d3d12core/d3d12core.dll artifacts/stage-dxr/ && \
  cp artifacts/build/vkd3d-proton-build/libs/d3d12/d3d12.dll artifacts/stage-dxr/ && \
  cd artifacts/stage-dxr && rm -f vkd3d-proton.cache* && \
  /tmp/run-probe.sh cr_inner_probe.exe && /tmp/run-probe.sh feedback_probe.exe && \
  /tmp/run-probe.sh mesh_probe.exe && /tmp/run-probe.sh corpus.exe

The MVK side was already fully repaired (the addMeshShaderToPipeline failure-handler restore at
line~1239: `if (!addMeshShaderToPipeline(...)) { [meshDesc release]; return nil; }`) and MVK rebuild
succeeded (mvkbuild71: build 0).
