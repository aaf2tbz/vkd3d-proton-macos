#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
// Verify: device-created placement-sparse texture + placement heap + updateTextureMappings
int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        // 1. placement-sparse texture from the DEVICE
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1024 height:1024 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        td.storageMode = MTLStorageModePrivate;
        td.placementSparsePageSize = MTLSparsePageSize64;
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        printf("device sparse texture: %s isSparse=%d tier=%d firstMipInTail=%d tail=%llu\n",
               tex ? "ok" : "FAIL", tex.isSparse, (int)tex.sparseTextureTier,
               (int)tex.firstMipmapInTail, (unsigned long long)tex.tailSizeInBytes);
        if (!tex) return 1;
        // 2. placement heap
        MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
        hd.type = MTLHeapTypePlacement;
        hd.size = 1ull << 24;  // 16 MB
        hd.storageMode = MTLStorageModePrivate;
        hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
        id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
        printf("placement heap: %s\n", heap ? "ok" : "FAIL");
        // 3. map the full texture (in tiles) via the queue
        id<MTLCommandQueue> q = [dev newCommandQueue];
        MTL4CommandQueueDescriptor *q4d = [MTL4CommandQueueDescriptor new];
        NSError *qerr = nil;
        id<MTL4CommandQueue> q4 = [dev newMTL4CommandQueueWithDescriptor:q4d error:&qerr];
        if (!q4) { printf("MTL4 queue fail: %s\n", qerr.localizedDescription.UTF8String); return 1; }
        printf("MTL4 queue: ok\n");
        MTLRegion region = MTLRegionMake2D(0, 0, 1024/64, 1024/64); // in tiles (64x64 px tiles)
        MTL4UpdateSparseTextureMappingOperation op;
        op.mode = MTLSparseTextureMappingModeMap;
        op.textureRegion = region;
        op.textureLevel = 0;
        op.textureSlice = 0;
        op.heapOffset = 0;
        if ([q4 respondsToSelector:@selector(updateTextureMappings:heap:operations:count:)]) {
            [q4 updateTextureMappings:tex heap:heap operations:&op count:1];
            printf("updateTextureMappings: called\n");
        } else {
            printf("updateTextureMappings: NOT AVAILABLE on queue\n");
        }
        // 4. write + readback through the mapped tiles
        id<MTLLibrary> lib = [dev newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
            @"kernel void k(texture2d<float, access::write> t [[texture(0)]]) {\n"
            @"  t.write(float4(0.1, 0.2, 0.3, 0.4), uint2(32, 32));\n"
            @"}\n" options:nil error:&err];
        id<MTLFunction> fn = [lib newFunctionWithName:@"k"];
        id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:fn error:&err];
        if (!pso) { printf("pso fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        // writer kernel (writes the mapped tile)
        id<MTLEvent> ev = [dev newEvent];
        [q4 signalEvent:ev value:1];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        [cb encodeWaitForEvent:ev value:1];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc setComputePipelineState:pso];
        [enc setTexture:tex atIndex:0];
        [enc dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        // EVENT-SYNCED BLIT TRANSFER TEST: mapping on q4, classic queue waits.
        {
            id<MTLEvent> ev2 = [dev newEvent];
            [q4 signalEvent:ev2 value:1];
            id<MTLCommandBuffer> cbt = [q commandBuffer];
            [cbt encodeWaitForEvent:ev2 value:1];
            id<MTLBlitCommandEncoder> blit = [cbt blitCommandEncoder];
            uint32_t val = 0x11223344;
            id<MTLBuffer> sb = [dev newBufferWithLength:256*256*4 options:MTLResourceStorageModeShared];
            uint32_t* pp = (uint32_t*)sb.contents;
            for (int i=0;i<256*256;i++) pp[i]=val;
            [blit copyFromBuffer:sb sourceOffset:0 sourceBytesPerRow:1024 sourceBytesPerImage:256*1024
                      sourceSize:MTLSizeMake(256,256,1)
                     toTexture:tex destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
            [blit endEncoding];
            [cbt commit]; [cbt waitUntilCompleted];
            id<MTLBuffer> rb = [dev newBufferWithLength:256*256*4 options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cbt2 = [q commandBuffer];
            id<MTLBlitCommandEncoder> blit2 = [cbt2 blitCommandEncoder];
            [blit2 copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                       sourceSize:MTLSizeMake(256,256,1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:1024 destinationBytesPerImage:256*1024];
            [blit2 endEncoding];
            [cbt2 commit]; [cbt2 waitUntilCompleted];
            uint32_t px = ((uint32_t*)rb.contents)[32*256+32];
            printf("event-synced blit readback: 0x%08x (expect 0x11223344)\n", px);
        }
        // reader kernel (reads the same texel into a buffer)
        id<MTLLibrary> lib2 = [dev newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
            @"kernel void r(texture2d<float, access::read> t [[texture(0)]], device float4* o [[buffer(0)]]) {\n"
            @"  o[0] = t.read(uint2(32, 32));\n"
            @"}\n" options:nil error:&err];
        id<MTLFunction> fn2 = [lib2 newFunctionWithName:@"r"];
        id<MTLComputePipelineState> pso2 = [dev newComputePipelineStateWithFunction:fn2 error:&err];
        float rv[4] = {-1,-1,-1,-1};
        id<MTLBuffer> rb = [dev newBufferWithBytes:rv length:16 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb3 = [q commandBuffer];
        id<MTLComputeCommandEncoder> enc3 = [cb3 computeCommandEncoder];
        [enc3 setComputePipelineState:pso2];
        [enc3 setTexture:tex atIndex:0];
        [enc3 setBuffer:rb offset:0 atIndex:0];
        [enc3 dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
        [enc3 endEncoding];
        [cb3 commit];
        [cb3 waitUntilCompleted];
        float *rr = (float*)rb.contents;
        printf("mapped-tile read: %f %f %f %f (expect 0.1 0.2 0.3 0.4)\n", rr[0], rr[1], rr[2], rr[3]);
        // UNMAP and read again (expect 0/opaque-black semantics)
        op.mode = MTLSparseTextureMappingModeUnmap;
        [q4 updateTextureMappings:tex heap:heap operations:&op count:1];
        [q4 signalEvent:ev value:2];
        id<MTLCommandBuffer> cb4 = [q commandBuffer];
        [cb4 encodeWaitForEvent:ev value:2];
        id<MTLComputeCommandEncoder> enc4 = [cb4 computeCommandEncoder];
        [enc4 setComputePipelineState:pso2];
        [enc4 setTexture:tex atIndex:0];
        [enc4 setBuffer:rb offset:0 atIndex:0];
        [enc4 dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
        [enc4 endEncoding];
        [cb4 commit];
        [cb4 waitUntilCompleted];
        float *rr2 = (float*)rb.contents;
        printf("unmapped-tile read: %f %f %f %f (expect 0 / opaque-black)\n", rr2[0], rr2[1], rr2[2], rr2[3]);
        fflush(stdout);
    }
    return 0;
}
