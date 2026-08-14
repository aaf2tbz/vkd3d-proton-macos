#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
// 1:1 replication of MVK's sparse texture descriptor
int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        MTLTextureDescriptor *td = [MTLTextureDescriptor new];
        td.textureType = MTLTextureType2D;
        td.pixelFormat = MTLPixelFormatBGRA8Unorm;
        td.width = 256; td.height = 256; td.depth = 1;
        td.mipmapLevelCount = 1; td.arrayLength = 1;
        td.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;  // 0x5
        td.storageMode = MTLStorageModePrivate;
        td.cpuCacheMode = MTLCPUCacheModeDefaultCache;
        td.allowGPUOptimizedContents = YES;
        td.placementSparsePageSize = MTLSparsePageSize64;
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        printf("repl texture: sparse=%d tier=%d\n", (int)tex.isSparse, (int)tex.sparseTextureTier);
        MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
        hd.type = MTLHeapTypePlacement; hd.size = 1024*1024; hd.storageMode = MTLStorageModePrivate;
        hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
        hd.hazardTrackingMode = MTLHazardTrackingModeTracked;
        id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
        printf("heap: %s\n", heap ? "ok" : "FAIL");
        MTL4CommandQueueDescriptor *qd = [MTL4CommandQueueDescriptor new];
        id<MTL4CommandQueue> q4 = [dev newMTL4CommandQueueWithDescriptor:qd error:&err];
        MTL4UpdateSparseTextureMappingOperation ops[16];
        for (int ty=0;ty<4;ty++) for(int tx=0;tx<4;tx++){
            ops[ty*4+tx].mode = MTLSparseTextureMappingModeMap;
            ops[ty*4+tx].textureRegion = MTLRegionMake2D(tx,ty,1,1);
            ops[ty*4+tx].textureLevel = 0; ops[ty*4+tx].textureSlice = 0;
            ops[ty*4+tx].heapOffset = (ty*4+tx)*4;  // page-multiple offsets for tier 2
        }
        for (int i = 0; i < 16; i++) { [q4 updateTextureMappings:tex heap:heap operations:&ops[i] count:1]; }
        printf("mappings: no abort\n");
        id<MTLEvent> ev = [dev newEvent];
        [q4 signalEvent:ev value:1];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        [cb encodeWaitForEvent:ev value:1];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        uint32_t val = 0x11223344;
        id<MTLBuffer> sb = [dev newBufferWithLength:256*256*4 options:MTLResourceStorageModeShared];
        uint32_t* pp = (uint32_t*)sb.contents;
        for (int i=0;i<256*256;i++) pp[i]=val;
        [blit copyFromBuffer:sb sourceOffset:0 sourceBytesPerRow:1024 sourceBytesPerImage:256*1024
                  sourceSize:MTLSizeMake(256,256,1)
                 toTexture:tex destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit endEncoding];
        [cb commit]; [cb waitUntilCompleted];
        printf("write cb status: %lu (1=error)\n", (unsigned long)cb.status);
        id<MTLBuffer> rb = [dev newBufferWithLength:256*256*4 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb2 = [q commandBuffer];
        id<MTLBlitCommandEncoder> blit2 = [cb2 blitCommandEncoder];
        [blit2 copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                   sourceSize:MTLSizeMake(256,256,1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:1024 destinationBytesPerImage:256*1024];
        [blit2 endEncoding];
        [cb2 commit]; [cb2 waitUntilCompleted];
        uint32_t px = ((uint32_t*)rb.contents)[32*256+32];
        printf("readback: 0x%08x (expect 0x11223344) status2=%lu\n", px, (unsigned long)cb2.status);
        printf("RESULT: %s\n", px==0x11223344 ? "TIER2 WORKS" : "TIER2 FAILED");
        fflush(stdout);
    }
    return 0;
}
