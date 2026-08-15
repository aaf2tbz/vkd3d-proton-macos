// M7b: TLAS + instancing query as a pure-Metal reference: BLAS -> instance -> TLAS,
// then a compute kernel with the instancing intersector (classic intersect()).
#import <Metal/Metal.h>
#import <simd/simd.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <math.h>
#define TRACE(fmt, ...) do { fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while(0)

static void commitAndWait(id<MTL4CommandQueue> q4, id<MTL4CommandBuffer> cb) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    MTL4CommitOptions *copts = [MTL4CommitOptions new];
    [copts addFeedbackHandler:^(id<MTL4CommitFeedback> fb) { dispatch_semaphore_signal(sem); }];
    [q4 commit:&cb count:1 options:copts];
    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        TRACE("device: %s", dev.name.UTF8String);

        float verts[9] = { 0,0,-5, 4,0,-5, 0,4,-5 };
        uint32_t indices[3] = { 0, 1, 2 };
        // MVK staging style: standalone buffers filled via the CPU memcpy
        id<MTLBuffer> vb = [dev newBufferWithLength:36 options:MTLResourceStorageModeShared];
        memcpy(vb.contents, verts, 36);
        id<MTLBuffer> ib = [dev newBufferWithLength:12 options:MTLResourceStorageModeShared];
        memcpy(ib.contents, indices, 12);
        TRACE("vb: %p addr=0x%llx", vb, (unsigned long long)vb.gpuAddress);
        MTL4BufferRange vbr = MTL4BufferRange(vb.gpuAddress, 36);
        MTL4BufferRange ibr = MTL4BufferRange(ib.gpuAddress, 12);

        MTL4AccelerationStructureTriangleGeometryDescriptor *geo = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
        geo.vertexBuffer = vbr;
        geo.vertexStride = 12;
        geo.indexBuffer = ibr;
        geo.indexType = MTLIndexTypeUInt32;
        geo.triangleCount = 1;
        MTL4PrimitiveAccelerationStructureDescriptor *bdesc = [MTL4PrimitiveAccelerationStructureDescriptor new];
        bdesc.geometryDescriptors = @[ geo ];
        MTLAccelerationStructureSizes bsizes = [dev accelerationStructureSizesWithDescriptor:bdesc];
        id<MTLBuffer> scratch = [dev newBufferWithLength:bsizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
        id<MTLAccelerationStructure> blas = [dev newAccelerationStructureWithSize:bsizes.accelerationStructureSize];
        TRACE("blas: %@ id=0x%llx", blas, (unsigned long long)blas.gpuResourceID._impl);

        id<MTLResidencySet> rez = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&err];
        [rez addAllocation:vb];
        [rez addAllocation:ib];
        [rez commit];
        id<MTL4CommandQueue> q4 = [dev newMTL4CommandQueueWithDescriptor:[MTL4CommandQueueDescriptor new] error:&err];
        id<MTL4CommandAllocator> alloc = [dev newCommandAllocator];
        id<MTL4CommandBuffer> cb = [dev newCommandBuffer];
        [cb beginCommandBufferWithAllocator:alloc];
        id<MTL4ComputeCommandEncoder> enc = [cb computeCommandEncoder];
        printf("vb addr=0x%llx ib addr=0x%llx scratch addr=0x%llx\n",
               (unsigned long long)vb.gpuAddress, (unsigned long long)ib.gpuAddress, (unsigned long long)scratch.gpuAddress);
        [enc buildAccelerationStructure:blas descriptor:bdesc scratchBuffer:MTL4BufferRange(scratch.gpuAddress, bsizes.buildScratchBufferSize)];
        [enc endEncoding];
        [cb endCommandBuffer];
        printf("pre-commit\n");
        commitAndWait(q4, cb);
        printf("BLAS built: resourceID=0x%llx\n", (unsigned long long)blas.gpuResourceID._impl);

        MTLIndirectAccelerationStructureInstanceDescriptor inst;
        inst.transformationMatrix.columns[0] = MTLPackedFloat3(1,0,0);
        inst.transformationMatrix.columns[1] = MTLPackedFloat3(0,1,0);
        inst.transformationMatrix.columns[2] = MTLPackedFloat3(0,0,1);
        inst.transformationMatrix.columns[3] = MTLPackedFloat3(0,0,0);
        inst.options = 0;
        inst.mask = 0xFF;
        inst.intersectionFunctionTableOffset = 0;
        inst.userID = 7;
        inst.accelerationStructureID = blas.gpuResourceID;
        id<MTLBuffer> instBuf = [dev newBufferWithBytes:&inst length:sizeof(inst) options:MTLResourceStorageModeShared];
        MTL4InstanceAccelerationStructureDescriptor *tdesc = [MTL4InstanceAccelerationStructureDescriptor new];
        tdesc.instanceDescriptorBuffer = MTL4BufferRange(instBuf.gpuAddress, sizeof(inst));
        tdesc.instanceCount = 1;
        tdesc.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
        MTLAccelerationStructureSizes tsizes = [dev accelerationStructureSizesWithDescriptor:tdesc];
        id<MTLAccelerationStructure> tlas = [dev newAccelerationStructureWithSize:tsizes.accelerationStructureSize];
        id<MTLBuffer> tscratch = [dev newBufferWithLength:tsizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
        id<MTL4CommandAllocator> alloc2 = [dev newCommandAllocator];
        id<MTL4CommandBuffer> cb2 = [dev newCommandBuffer];
        [cb2 beginCommandBufferWithAllocator:alloc2];
        id<MTL4ComputeCommandEncoder> enc2 = [cb2 computeCommandEncoder];
        [enc2 buildAccelerationStructure:tlas descriptor:tdesc scratchBuffer:MTL4BufferRange(tscratch.gpuAddress, tsizes.buildScratchBufferSize)];
        [enc2 endEncoding];
        [cb2 endCommandBuffer];
        commitAndWait(q4, cb2);
        printf("TLAS built: resourceID=0x%llx\n", (unsigned long long)tlas.gpuResourceID._impl);

        id<MTLLibrary> lib = [dev newLibraryWithSource:@R"MSL(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;

// MVK-style argument data: the descriptor set is a RAW buffer whose member 1
// holds the acceleration structure resource ID (MVKGPUResource array).
struct SpvDescSet
{
    constant void* pad0 [[id(0)]];
    acceleration_structure<instancing> as [[id(1)]];
    device float* hitsPtr [[id(2)]];
};

intersection_params spvMakeIntersectionParams(uint flags)
{
    intersection_params ip;
    if ((flags & 1) != 0)
        ip.force_opacity(forced_opacity::opaque);
    if ((flags & 2) != 0)
        ip.force_opacity(forced_opacity::non_opaque);
    if ((flags & 4) != 0)
        ip.accept_any_intersection(true);
    if ((flags & 16) != 0)
        ip.set_triangle_cull_mode(triangle_cull_mode::back);
    if ((flags & 32) != 0)
        ip.set_triangle_cull_mode(triangle_cull_mode::front);
    if ((flags & 64) != 0)
        ip.set_opacity_cull_mode(opacity_cull_mode::opaque);
    if ((flags & 128) != 0)
        ip.set_opacity_cull_mode(opacity_cull_mode::non_opaque);
    if ((flags & 256) != 0)
        ip.set_geometry_cull_mode(geometry_cull_mode::triangle);
    if ((flags & 512) != 0)
        ip.set_geometry_cull_mode(geometry_cull_mode::bounding_box);
    return ip;
}

kernel void raycastRQ(device float* hits [[buffer(0)]],
                      uint2 tid [[thread_position_in_grid]],
                      uint2 gsize [[threads_per_grid]],
                      constant SpvDescSet& ds [[buffer(2)]],
                      device float* hits2 [[buffer(3)]])
{
    uint idx = tid.y * gsize.x + tid.x;
    float u = (float)tid.x / (float)gsize.x;
    float v = (float)tid.y / (float)gsize.y;
    float3 origin = float3(2.0, 2.0, 0.0);
    float3 dir = normalize(float3((u - 0.5) * 8.0, (v - 0.5) * 8.0, -5.0));
    ray r(origin, dir, 0.0f, 1000.0f);
    intersection_query<instancing, triangle_data> _50;
    _50.reset(r, ds.as, 255u, spvMakeIntersectionParams(0u));
    bool _60 = _50.next();
    uint _61 = uint(_50.get_candidate_intersection_type());
    if (_61 == 1u) {
        float _66 = _50.get_candidate_triangle_distance();
        hits[idx] = _66;
    } else {
        hits[idx] = -1.0f;
    }
    hits2[idx] = (float)_61;
}
)MSL" options:nil error:&err];
        if (!lib) { printf("lib fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        id<MTLFunction> fn = [lib newFunctionWithName:@"raycastRQ"];
        id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:fn error:&err];
        if (!pso) { printf("pso fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        printf("compute pso: OK\n");

        const int W = 16, H = 16;
        id<MTLBuffer> hits = [dev newBufferWithLength:W*H*4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> hits2 = [dev newBufferWithLength:W*H*4 options:MTLResourceStorageModeShared];
        // MTL4 argument-table path with the ds at [[buffer(0)]]
        id<MTL4CommandAllocator> alloc3 = [dev newCommandAllocator];
        id<MTL4CommandBuffer> cb3 = [dev newCommandBuffer];
        [cb3 beginCommandBufferWithAllocator:alloc3];
        id<MTL4ComputeCommandEncoder> enc3 = [cb3 computeCommandEncoder];
        [enc3 setComputePipelineState:pso];
        struct { uint64_t pad0; MTLResourceID asID; uint64_t hitsAddr; } argData;
        argData.pad0 = 4096;
        argData.asID = tlas.gpuResourceID;
        argData.hitsAddr = hits.gpuAddress;
        id<MTLBuffer> argBuf = [dev newBufferWithBytes:&argData length:sizeof(argData) options:MTLResourceStorageModeShared];
        MTL4ArgumentTableDescriptor *atd = [MTL4ArgumentTableDescriptor new];
        atd.maxBufferBindCount = 4;
        id<MTL4ArgumentTable> atab = [dev newArgumentTableWithDescriptor:atd error:&err];
        [atab setAddress:argBuf.gpuAddress atIndex:2];
        [atab setResource:tlas.gpuResourceID atBufferIndex:2];
        [enc3 setArgumentTable:atab];
        [enc3 dispatchThreads:MTLSizeMake(W,H,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
        [enc3 endEncoding];
        [cb3 endCommandBuffer];
        commitAndWait(q4, cb3);

        float *hp = (float*)hits.contents;
        float *hp2 = (float*)hits2.contents;
        printf("raw type at thread 0: %f\n", hp2[0]);
        int typec[4] = {0,0,0,0};
        for (int i = 0; i < W*H; i++) { int t = (int)hp2[i]; if (t>=0 && t<4) typec[t]++; }
        printf("type dist: none=%d tri=%d bbox=%d other=%d\n", typec[0], typec[1], typec[2], typec[3]);
        int hitsN = 0; float minD = 1e9;
        for (int i = 0; i < W*H; i++) {
            if (hp[i] > 0) { hitsN++; if (hp[i] < minD) minD = hp[i]; }
        }
        float maxD = 0;
        for (int i = 0; i < W*H; i++) if (hp[i] > 0 && hp[i] > maxD) maxD = hp[i];
        printf("TLAS instancing hits: %d minD=%.3f maxD=%.3f\n", hitsN, minD, maxD);
        int cts[4] = {0,0,0,0}; float dmin = 1e9, dmax = 0;
        for (int i = 0; i < W*H; i++) if (hp[i] > 0) {
            int ct = (int)(hp[i] / 1000.0f);
            float d = hp[i] - ct*1000.0f;
            if (ct >= 0 && ct < 4) cts[ct]++;
            if (d < dmin) dmin = d; if (d > dmax) dmax = d;
        }
        printf("ct distribution: none=%d tri=%d bbox=%d other=%d  dist range %.3f..%.3f\n",
               cts[0], cts[1], cts[2], cts[3], dmin, dmax);
        float cmin = 1e9, cmax = 0; int chits = 0;
        for (int i = 0; i < W*H; i++) if (hp2[i] > 0) { chits++; if (hp2[i] < cmin) cmin = hp2[i]; if (hp2[i] > cmax) cmax = hp2[i]; }
        printf("candidate distance: hits=%d range %.3f..%.3f\n", chits, cmin, cmax);
        return hitsN > 0 ? 0 : 2;
    }
}
