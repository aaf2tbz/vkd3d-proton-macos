#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
// Test: does Metal support min/max filtering (sampler reduction) on Apple M4?
int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        // sampler with min/max filter
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterMin;
        sd.magFilter = MTLSamplerMinMagFilterMax;
        id<MTLSamplerState> sampler = [dev newSamplerStateWithDescriptor:sd];
        printf("min/max sampler: %s\n", sampler ? "CREATED" : "FAILED");
        // verify behavior: sample a texture with two values via compute
        id<MTLLibrary> lib = [dev newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
            @"kernel void k(texture2d<float, access::read> t [[texture(0)]],\n"
            @"               sampler s [[sampler(0)]], device float4* o [[buffer(0)]]) {\n"
            @"  o[0] = t.sample(s, float2(0.25));\n"   // between the two texels
            @"}\n" options:nil error:&err];
        if (!lib || !sampler) { printf("setup fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        id<MTLFunction> fn = [lib newFunctionWithName:@"k"];
        id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:fn error:&err];
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:2 height:1 mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        float data[2] = { 0.2f, 0.8f };
        [tex replaceRegion:MTLRegionMake2D(0,0,2,1) mipmapLevel:0 withBytes:data bytesPerRow:8];
        float outv[4] = { -1, -1, -1, -1 };
        id<MTLBuffer> ob = [dev newBufferWithBytes:outv length:16 options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc setComputePipelineState:pso];
        [enc setTexture:tex atIndex:0];
        [enc setSamplerState:sampler atIndex:0];
        [enc setBuffer:ob offset:0 atIndex:0];
        [enc dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
        [enc endEncoding];
        [cb commit]; [cb waitUntilCompleted];
        float *r = (float*)ob.contents;
        printf("min/max sample result: %f (min=0.2 max=0.8; between -> min filter gives 0.2)\n", r[0]);
        printf("RESULT: %s\n", (r[0] == 0.2f) ? "MIN/MAX FILTERING WORKS ON M4" : "not min/max");
        fflush(stdout);
    }
    return 0;
}
