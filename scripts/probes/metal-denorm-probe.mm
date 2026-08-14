#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <float.h>

static void run_case(id<MTLDevice> dev, id<MTLCommandQueue> q, bool fastMath, const char* label) {
    NSError *err = nil;
    MTLCompileOptions *opts = [MTLCompileOptions new];
    opts.fastMathEnabled = fastMath ? YES : NO;
    id<MTLLibrary> lib = [dev newLibraryWithSource:
        @"#include <metal_stdlib>\nusing namespace metal;\n"
        @"kernel void k(device float* i [[buffer(0)]], device float* o [[buffer(1)]]) {\n"
        @"  o[0] = i[0] * 2.0f;\n"
        @"  o[1] = 1.0f / i[0];  // denormal reciprocal\n"
        @"}\n" options:opts error:&err];
    if (!lib) { printf("%-22s: lib fail %s\n", label, err.localizedDescription.UTF8String); return; }
    id<MTLFunction> fn = [lib newFunctionWithName:@"k"];
    id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:fn error:&err];
    if (!pso) { printf("%-22s: pso fail\n", label); return; }
    float inData[2] = { FLT_MIN / 2.0f, 0 };
    float outData[2] = { -1, -1 };
    id<MTLBuffer> ib = [dev newBufferWithBytes:inData length:8 options:MTLResourceStorageModeShared];
    id<MTLBuffer> ob = [dev newBufferWithBytes:outData length:8 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    [enc setComputePipelineState:pso];
    [enc setBuffer:ib offset:0 atIndex:0];
    [enc setBuffer:ob offset:0 atIndex:1];
    [enc dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    float *r = (float*)ob.contents;
    const char *verdict = (r[0] == FLT_MIN) ? "PRESERVED" : (r[0] == 0.0f ? "FTZ" : "OTHER");
    printf("%-22s: denorm*2 = %g  -> %s\n", label, r[0], verdict);
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> q = [dev newCommandQueue];
        run_case(dev, q, true,  "fastMath=YES (MVK dflt)");
        run_case(dev, q, false, "fastMath=NO");
    }
    return 0;
}
