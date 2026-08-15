// M7c isolation: separates BLAS health / classic intersector / TLAS instancing /
// intersection_query API. Kernel A = classic intersector on the BLAS (the known
// good M7 pattern). Kernel B = intersection_query on the TLAS (the SPIRV-Cross
// shape). Both use DIRECT resource bindings (no struct-member raw data).
#import <Metal/Metal.h>
#import <simd/simd.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <math.h>

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
        printf("device: %s\n", dev.name.UTF8String);

        float verts[9] = { 0,0,-5, 4,0,-5, 0,4,-5 };
        uint32_t indices[3] = { 0, 1, 2 };
        // MVK pattern: the vertex data lives in a placement heap; the build stages
        // it into standalone shared buffers (CPU-written heap data is invisible to
        // MTL4 AS builds on this beta).
        MTLHeapDescriptor *vhD = [MTLHeapDescriptor new];
        vhD.type = MTLHeapTypePlacement;
        vhD.storageMode = MTLStorageModeShared;
        vhD.size = 1 << 20;
        id<MTLHeap> vh = [dev newHeapWithDescriptor:vhD];
        id<MTLBuffer> vhBuf = [vh newBufferWithLength:4096 options:0 offset:0];
        memcpy(vhBuf.contents, verts, 36);
        memcpy((char*)vhBuf.contents + 512, indices, 12);
        id<MTLBuffer> vb = [dev newBufferWithLength:36 options:MTLResourceStorageModeShared];
        memcpy(vb.contents, (char*)vhBuf.contents, 36);
        id<MTLBuffer> ib = [dev newBufferWithLength:12 options:MTLResourceStorageModeShared];
        memcpy(ib.contents, (char*)vhBuf.contents + 512, 12);
        MTL4BufferRange vbr = MTL4BufferRange(vb.gpuAddress, 36);
        MTL4BufferRange ibr = MTL4BufferRange(ib.gpuAddress, 12);

        MTL4AccelerationStructureTriangleGeometryDescriptor *geo = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
        geo.vertexBuffer = vbr;
        geo.vertexStride = 12;
        geo.indexBuffer = ibr;
        geo.indexType = MTLIndexTypeUInt32;
        geo.triangleCount = 1;
        geo.vertexFormat = MTLAttributeFormatFloat3;
        geo.intersectionFunctionTableOffset = 0;
        geo.opaque = NO;
        MTL4PrimitiveAccelerationStructureDescriptor *bdesc = [MTL4PrimitiveAccelerationStructureDescriptor new];
        bdesc.geometryDescriptors = @[ geo ];
        MTLAccelerationStructureSizes bsizes = [dev accelerationStructureSizesWithDescriptor:bdesc];
        printf("BLAS size: %llu scratch: %llu\n", (unsigned long long)bsizes.accelerationStructureSize, (unsigned long long)bsizes.buildScratchBufferSize);
        id<MTLBuffer> scratch = [dev newBufferWithLength:bsizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
        id<MTLAccelerationStructure> blas = [dev newAccelerationStructureWithSize:bsizes.accelerationStructureSize];

        id<MTL4CommandQueue> q4 = [dev newMTL4CommandQueueWithDescriptor:[MTL4CommandQueueDescriptor new] error:&err];
        id<MTL4CommandAllocator> alloc = [dev newCommandAllocator];
        id<MTL4CommandBuffer> cb = [dev newCommandBuffer];
        [cb beginCommandBufferWithAllocator:alloc];
        id<MTL4ComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc buildAccelerationStructure:blas descriptor:bdesc scratchBuffer:MTL4BufferRange(scratch.gpuAddress, bsizes.buildScratchBufferSize)];
        [enc endEncoding];
        [cb endCommandBuffer];
        commitAndWait(q4, cb);
        printf("BLAS built: id=0x%llx\n", (unsigned long long)blas.gpuResourceID._impl);

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
        printf("TLAS size: %llu scratch: %llu\n", (unsigned long long)tsizes.accelerationStructureSize, (unsigned long long)tsizes.buildScratchBufferSize);
        id<MTLAccelerationStructure> tlas = [dev newAccelerationStructureWithSize:4096];
        id<MTLBuffer> tscratch = [dev newBufferWithLength:tsizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
        id<MTL4CommandAllocator> alloc2 = [dev newCommandAllocator];
        id<MTL4CommandBuffer> cb2 = [dev newCommandBuffer];
        [cb2 beginCommandBufferWithAllocator:alloc2];
        id<MTL4ComputeCommandEncoder> enc2 = [cb2 computeCommandEncoder];
        [enc2 buildAccelerationStructure:tlas descriptor:tdesc scratchBuffer:MTL4BufferRange(tscratch.gpuAddress, tsizes.buildScratchBufferSize)];
        [enc2 endEncoding];
        [cb2 endCommandBuffer];
        commitAndWait(q4, cb2);
        printf("TLAS built: id=0x%llx\n", (unsigned long long)tlas.gpuResourceID._impl);

        // Two kernels: A = classic intersector on BLAS, B = intersection_query on TLAS
        id<MTLLibrary> lib = [dev newLibraryWithSource:@R"MSL(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;

kernel void rayA(device float* hits [[buffer(0)]],
                 uint2 tid [[thread_position_in_grid]],
                 uint2 gsize [[threads_per_grid]],
                 acceleration_structure<> accel [[buffer(2)]])
{
    uint idx = tid.y * gsize.x + tid.x;
    float u = (float)tid.x / (float)gsize.x;
    float v = (float)tid.y / (float)gsize.y;
    float3 origin = float3(2.0, 2.0, 0.0);
    float3 dir = normalize(float3((u - 0.5) * 8.0, (v - 0.5) * 8.0, -5.0));
    ray r(origin, dir, 0.0f, 1000.0f);
    intersector<triangle_data> i;
    auto hit = i.intersect(r, accel);
    hits[idx] = (hit.type == intersection_type::triangle) ? hit.distance : -1.0f;
}

// the EXACT SPIRV-Cross MSL kernel from the MVK path
struct _6
{
    float _m0[1];
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

kernel void rayB(device _6& _4 [[buffer(9)]],
                 uint2 tid [[thread_position_in_grid]],
                 uint2 gsize [[threads_per_grid]],
                 acceleration_structure<instancing> _5 [[buffer(8)]])
{
    uint idx = tid.y * gsize.x + tid.x;
    float u = (float)tid.x / (float)gsize.x;
    float v = (float)tid.y / (float)gsize.y;
    float3 origin = float3(2.0, 2.0, 0.0);
    float3 dir = normalize(float3((u - 0.5) * 8.0, (v - 0.5) * 8.0, -5.0));
    ray r(origin, dir, 0.0f, 1000.0f);
    intersection_query<instancing, triangle_data> _50;
    _50.reset(r, _5, 255u, spvMakeIntersectionParams(0u));
    bool _60 = _50.next();
    uint _61 = uint(_50.get_candidate_intersection_type());
    if (_61 == 1u)
    {
        float _66 = _50.get_candidate_triangle_distance();
        _4._m0[(tid.y * 64u) + tid.x] = _66;
    }
}
)MSL" options:nil error:&err];
        if (!lib) { printf("lib fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        id<MTLFunction> fnA = [lib newFunctionWithName:@"rayA"];
        id<MTLComputePipelineState> psoA = [dev newComputePipelineStateWithFunction:fnA error:&err];
        id<MTLFunction> fnB = [lib newFunctionWithName:@"rayB"];
        id<MTLComputePipelineState> psoB = [dev newComputePipelineStateWithFunction:fnB error:&err];
        if (!psoA || !psoB) { printf("pso fail\n"); return 1; }

        const int W = 16, H = 16;
        // MVK-style: the hits buffers live in a placement heap
        MTLHeapDescriptor *hitsHeapD = [MTLHeapDescriptor new];
        hitsHeapD.type = MTLHeapTypePlacement;
        hitsHeapD.storageMode = MTLStorageModeShared;
        hitsHeapD.size = 1 << 20;
        id<MTLHeap> hitsHeap = [dev newHeapWithDescriptor:hitsHeapD];
        id<MTLBuffer> hitsA = [hitsHeap newBufferWithLength:W*H*4 options:0 offset:131072];
        // classic-style raw argument data for the BLAS intersector
        struct { uint64_t pad; MTLResourceID asID; } argDataA;
        argDataA.pad = 0;
        argDataA.asID = blas.gpuResourceID;
        id<MTLBuffer> argBufA = [dev newBufferWithBytes:&argDataA length:sizeof(argDataA) options:MTLResourceStorageModeShared];
        id<MTLBuffer> hitsB = [dev newBufferWithLength:W*H*4 options:MTLResourceStorageModeShared];
        id<MTLResidencySet> rezHits = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&err];
        [rezHits addAllocation:hitsA];
        [rezHits addAllocation:hitsB];
        [rezHits commit];
        float *h0 = (float*)hitsB.contents;
        for (int i = 0; i < 16; i++) h0[i] = 77.0f;
        float *h1 = (float*)hitsB.contents;
        printf("heap CPU roundtrip: %f\n", h1[3]);

        MTL4ArgumentTableDescriptor *atd = [MTL4ArgumentTableDescriptor new];
        atd.maxBufferBindCount = 31;

        // dispatch A (BLAS + classic intersector) - CLASSIC encoder with the heap buffer
        id<MTLCommandQueue> cqA = [dev newCommandQueue];
        id<MTLCommandBuffer> c3 = [cqA commandBuffer];
        id<MTLComputeCommandEncoder> e3 = [c3 computeCommandEncoder];
        [e3 setComputePipelineState:psoA];
        [e3 setBuffer:hitsA offset:0 atIndex:0];
        [e3 setBuffer:argBufA offset:0 atIndex:2];
        [e3 useResource:blas usage:MTLResourceUsageRead];
        [e3 dispatchThreads:MTLSizeMake(W,H,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
        [e3 endEncoding];
        [c3 commit];
        [c3 waitUntilCompleted];

        // dispatch B (TLAS + intersection_query)
        id<MTL4CommandAllocator> a4 = [dev newCommandAllocator];
        id<MTL4CommandBuffer> c4 = [dev newCommandBuffer];
        [c4 beginCommandBufferWithAllocator:a4];
        id<MTL4ComputeCommandEncoder> e4 = [c4 computeCommandEncoder];
        [e4 setComputePipelineState:psoB];
        id<MTL4ArgumentTable> tB = [dev newArgumentTableWithDescriptor:atd error:&err];
        [tB setAddress:hitsB.gpuAddress atIndex:9];
        [tB setResource:tlas.gpuResourceID atBufferIndex:8];
        [e4 setArgumentTable:tB];
        [e4 dispatchThreads:MTLSizeMake(8,8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
        [e4 endEncoding];
        [c4 endCommandBuffer];
        commitAndWait(q4, c4);

        float *hA = (float*)hitsA.contents;
        float *hB = (float*)hitsB.contents;
        int nA = 0, nB = 0; float minA = 1e9, minB = 1e9;
        for (int i = 0; i < W*H; i++) {
            if (hA[i] > 0) { nA++; if (hA[i] < minA) minA = hA[i]; }
            if (hB[i] > 0) { nB++; if (hB[i] < minB) minB = hB[i]; }
        }
        printf("A (BLAS+intersector): hits=%d minD=%.3f\n", nA, minA);
        printf("B (TLAS+query): hits=%d minD=%.3f\n", nB, minB);
        printf("RESULT: %s\n", (nA > 0 && nB > 0) ? "BOTH WORK" : (nA > 0 ? "A OK, B BROKEN" : "A BROKEN"));
        return 0;
    }
}
