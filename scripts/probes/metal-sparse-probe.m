#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) { printf("no Metal device\n"); return 1; }
        printf("device: %s\n", dev.name.UTF8String);

        // Placement heap for sparse resources
        MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
        hd.type = MTLHeapTypeSparse;   /* classic sparse heap (macOS 11+) */
        hd.size = 1ull << 23; // 8 MB
        hd.storageMode = MTLStorageModePrivate;
        hd.sparsePageSize = MTLSparsePageSize64;
        NSError *err = nil;
        id<MTLHeap> heap = [dev newHeapWithDescriptor:hd];
        printf("placement heap: %s (maxCompatiblePlacementSparsePageSize=%d)\n",
               heap ? "ok" : "FAILED", (int)hd.maxCompatiblePlacementSparsePageSize);

        if (!heap) return 1;

        // Sparse texture from the placement heap
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1024 height:1024 mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        
        id<MTLTexture> tex = [heap newTextureWithDescriptor:td];
        printf("sparse texture: %s\n", tex ? "ok" : "FAILED");
        if (tex) {
            printf("  isSparse          : %d\n", tex.isSparse);
            printf("  sparseTextureTier : %d (0=none,1=tier1,2=tier2)\n", (int)tex.sparseTextureTier);
            printf("  firstMipmapInTail : %d\n", (int)tex.firstMipmapInTail);
            printf("  tailSizeInBytes   : %llu\n", (unsigned long long)tex.tailSizeInBytes);
        }

        // Sparse buffer from the placement heap
        id<MTLBuffer> buf = [heap newBufferWithLength:(1 << 16) options:MTLResourceStorageModePrivate];
        printf("sparse buffer: %s\n", buf ? "ok" : "FAILED");
        if (buf && [buf respondsToSelector:@selector(sparseTextureTier)]) printf("  buffer has tier property\n");

        // tile size
        MTLSize tile = [dev sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:MTLPixelFormatRGBA8Unorm sampleCount:1];
        printf("tile size 2D RGBA8: %lux%lu, device sparseTileSizeInBytes=%llu\n",
               (unsigned long)tile.width, (unsigned long)tile.height,
               (unsigned long long)[dev sparseTileSizeInBytes]);
    }
    return 0;
}
