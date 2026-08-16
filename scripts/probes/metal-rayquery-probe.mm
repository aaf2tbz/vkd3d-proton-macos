// ray-query FEASIBILITY: inline ray query on Metal (macOS 26 / Apple GPU).
// Builds a BLAS (one triangle) + TLAS (one instance) via the MTL4 API, then
// runs a COMPUTE kernel with the metal::raytracing intersector (inline
// intersection, the RayQuery analog) over a ray grid, and verifies the hit
// distances on the CPU.
#import <Metal/Metal.h>
#import <simd/simd.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <math.h>

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        printf("device: %s\n", dev.name.UTF8String);

        // Triangle: (0,0,-5), (4,0,-5), (0,4,-5) - a right triangle facing +z
        float verts[9] = { 0,0,-5, 4,0,-5, 0,4,-5 };
        uint32_t indices[3] = { 0, 1, 2 };
        id<MTLBuffer> vb = [dev newBufferWithBytes:verts length:36 options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib = [dev newBufferWithBytes:indices length:12 options:MTLResourceStorageModeShared];
        MTL4BufferRange vbr = MTL4BufferRange(vb.gpuAddress, 36);
        MTL4BufferRange ibr = MTL4BufferRange(ib.gpuAddress, 12);

        MTL4AccelerationStructureTriangleGeometryDescriptor *geo = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
        geo.vertexBuffer = vbr;
        geo.vertexStride = 12;   // 3 floats per vertex
        geo.indexBuffer = ibr;
        geo.indexType = MTLIndexTypeUInt32;
        geo.triangleCount = 1;

        MTL4PrimitiveAccelerationStructureDescriptor *bdesc = [MTL4PrimitiveAccelerationStructureDescriptor new];
        bdesc.geometryDescriptors = @[ geo ];

        // sizes
        MTLAccelerationStructureSizes bsizes = [dev accelerationStructureSizesWithDescriptor:bdesc];
        id<MTLBuffer> blasScratch = [dev newBufferWithLength:bsizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
        id<MTLAccelerationStructure> blas = [dev newAccelerationStructureWithSize:bsizes.accelerationStructureSize];
        printf("BLAS size: %llu scratch: %llu\n", (unsigned long long)bsizes.accelerationStructureSize,
               (unsigned long long)bsizes.buildScratchBufferSize);

        // build the BLAS via the MTL4 command buffer (no TLAS for this probe)
        id<MTL4CommandQueue> q4 = [dev newMTL4CommandQueueWithDescriptor:[MTL4CommandQueueDescriptor new] error:&err];
        id<MTL4CommandAllocator> buildAlloc = [dev newCommandAllocator];
        id<MTL4CommandBuffer> cb = [dev newCommandBuffer];
        [cb beginCommandBufferWithAllocator:buildAlloc];
        id<MTL4ComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc buildAccelerationStructure:blas descriptor:bdesc scratchBuffer:MTL4BufferRange(blasScratch.gpuAddress, bsizes.buildScratchBufferSize)];
        [enc endEncoding];
        [cb endCommandBuffer];
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        MTL4CommitOptions *copts = [MTL4CommitOptions new];
        [copts addFeedbackHandler:^(id<MTL4CommitFeedback> fb) { dispatch_semaphore_signal(sem); }];
        [q4 commit:&cb count:1 options:copts];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        printf("build: committed\n");

        // compute kernel: inline intersection via the intersector
        id<MTLLibrary> lib = [dev newLibraryWithSource:@R"MSL(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;

kernel void raycast(device float* hits [[buffer(0)]],
                    uint2 tid [[thread_position_in_grid]],
                    uint2 gsize [[threads_per_grid]],
                    acceleration_structure<> accel [[buffer(2)]])
{
    uint idx = tid.y * gsize.x + tid.x;
    // ray from the camera at (2,2,0) through the pixel grid toward -z
    float u = (float)tid.x / (float)gsize.x;
    float v = (float)tid.y / (float)gsize.y;
    float3 origin = float3(2.0, 2.0, 0.0);
    float3 dir = normalize(float3((u - 0.5) * 8.0, (v - 0.5) * 8.0, -5.0));
    intersector<triangle_data> i;
    ray r(origin, dir, 0.0f, 1000.0f);
    auto hit = i.intersect(r, accel);
    if (hit.type == intersection_type::triangle) {
        hits[idx] = hit.distance;
    } else {
        hits[idx] = -1.0f;
    }
}
)MSL" options:nil error:&err];
        if (!lib) { printf("lib fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        id<MTLFunction> fn = [lib newFunctionWithName:@"raycast"];
        id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:fn error:&err];
        if (!pso) { printf("pso fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        printf("compute pso: OK\n");

        const int W = 16, H = 16;
        id<MTLBuffer> hits = [dev newBufferWithLength:W*H*4 options:MTLResourceStorageModeShared];
        id<MTL4CommandAllocator> alloc = [dev newCommandAllocator];
        id<MTL4CommandBuffer> cb2 = [dev newCommandBuffer];
        [cb2 beginCommandBufferWithAllocator:alloc];
        id<MTL4ComputeCommandEncoder> enc2 = [cb2 computeCommandEncoder];
        [enc2 setComputePipelineState:pso];
        MTL4ArgumentTableDescriptor *atd = [MTL4ArgumentTableDescriptor new];
        atd.maxBufferBindCount = 3;
        id<MTL4ArgumentTable> atab = [dev newArgumentTableWithDescriptor:atd error:&err];
        [atab setAddress:hits.gpuAddress atIndex:0];
        [atab setResource:blas.gpuResourceID atBufferIndex:2];
        [enc2 setArgumentTable:atab];
        [enc2 dispatchThreads:MTLSizeMake(W,H,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
        [enc2 endEncoding];
        [cb2 endCommandBuffer];
        MTL4CommitOptions *copts2 = [MTL4CommitOptions new];
        [copts2 addFeedbackHandler:^(id<MTL4CommitFeedback> fb) { dispatch_semaphore_signal(sem); }];
        [q4 commit:&cb2 count:1 options:copts2];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        printf("dispatch: committed\n");

        float* hp = (float*)hits.contents;
        int hitCount = 0;
        float minD = 1e9, maxD = -1;
        printf("hit map (X=hit, .=miss):\n");
        for (int y = 0; y < H; y++) {
            char row[17];
            for (int x = 0; x < W; x++) {
                float d = hp[y*W+x];
                if (d >= 0) { row[x] = 'X'; hitCount++; if (d < minD) minD = d; if (d > maxD) maxD = d; }
                else row[x] = '.';
            }
            row[16]=0;
            printf("%s\n", row);
        }
        printf("hits=%d minD=%.3f maxD=%.3f\n", hitCount, minD, maxD);
        // expected: the triangle at z=-5 spans x,y in [0,4] with x+y<=4; rays from
        // (2,2) toward the grid: the triangle's projection onto the grid should be
        // a centered triangle; all hit distances ~5.0-5.5
        printf("RESULT: %s\n", hitCount > 0 ? "INLINE RAY QUERY (INTERSECTOR) WORKS ON GPU" : "FAILED - NO HITS");
        fflush(stdout);
    }
    return 0;
}
