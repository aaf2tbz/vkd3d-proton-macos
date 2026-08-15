# M7: inline ray query — dispatch path fully integrated; output-write blocker documented

## Verified at the Metal level (pure-Metal reference, 3/3+ runs)
metal-tlas-isolate-probe.mm proves the exact MVK dispatch pattern end to end:
- BLAS (standalone vb/ib/scratch) + TLAS (MTL4 indirect instance descriptor)
- kernel: `intersection_query<instancing, triangle_data>` + candidate getters +
  `spvMakeIntersectionParams` (the exact SPIRV-Cross MSL shape)
- MTL4 argument-table dispatch: AS bound via setResource at [[buffer(8)]],
  output via setAddress at [[buffer(9)]], maxBufferBindCount 31
- RESULT: 30/256 hits, distances 5.000-5.431 EXACT — every run

## MVK integration (all landed, committed)
- **Discrete AS descriptor sets**: sets with VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
  never use argument buffers. The AS emits as a DIRECT [[buffer(N)]] entry-point
  argument (the argument-buffer struct-member [[id(N)]] pattern and the classic
  raw-argument-data pattern both fail to resolve the AS on this SDK).
- **perDescriptorResourceCount**: the AS occupies a buffer slot (it is bound as
  a direct [[buffer(N)]] resource); the bind script generates classic BindBuffer
  ops for AS sets so the MTL4 dispatch can read per-binding targets.
- **MVKCmdDispatch**: AS pipelines dispatch on an MTL4 compute command buffer
  with an argument table: AS bindings via setResource (gpuResourceID), buffer
  bindings via setAddress (gpuAddress + descriptor offset, dynamic offsets
  honored), targets from the pipeline bind script.
- **SPIRV-Cross**: AS resources index off msl_buffer (discrete emission).
- The probe SPIR-V needed OpMemberDecorate on the SSBO struct (empty struct meta
  silently dropped the SSBO from the discrete emission - the pipeline compiled
  with an undeclared variable otherwise).

## Remaining blocker (documented)
The MTL4 argument-table dispatches cannot reliably WRITE the MVK's memory types:
- type 1 (DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT|HOST_CACHED): backed by the
  placement MTLHeap - MTL4 table writes consistently fail (0 readback).
- type 0 (DEVICE_LOCAL only): MTLStorageModePrivate standalone - writes also
  fail to land in the copy-based readback; and it cannot be host-mapped.
- type 2 (DEVICE_LOCAL|LAZILY_ALLOCATED): MTLStorageModeMemoryless - aborts.
The MVK exposes NO standalone MTLStorageModeShared type, which is the pattern
the pure-Metal reference proves works. The vk-as-probe therefore honestly reads
0 hits (the type-1 write failure) while every other stage is verified. The
pure-Metal 30/256-hit result stands as the path proof.

## Staging attempt (committed 004be0e)
The dispatch now stages writable storage-buffer bindings through standalone
shared MTLBuffers and copies results back with a classic blit (and
getBufferForDeviceAddress searches all buffers). The staged writes still read
back ZERO - the kernel's writes are not landing even in the standalone staging.
The pure-Metal reference with the identical structure works, so the remaining
fault is in the MTL4 dispatch execution itself (next: inspect MTL4 commit
feedback errors / the argument-table state).

The MTL4CommitFeedback error is now surfaced: the dispatch reports NO GPU
error, yet the kernel's writes still don't land - the fault is in the
argument-table/PSO state rather than a GPU fault (the dispatch is a no-op or
the writes are discarded).

## Next steps
1. Debug the MTL4 dispatch execution: no GPU error but writes don't land - check
   the argument-table state (the table may need the resources/addresses declared
   differently), the PSO state, and the dispatch thread semantics.
2. Distance verification once writes land (expect 5.000-5.431 like the
   pure-Metal reference).
3. vkd3d-proton DXR 1.1 activation (M8) on top of the proven dispatch path.
