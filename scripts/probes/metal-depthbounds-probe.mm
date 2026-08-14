#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>

static id<MTLRenderPipelineState> makePSO(id<MTLDevice> dev, MTLPixelFormat fmt, NSError **err) {
    id<MTLLibrary> lib = [dev newLibraryWithSource:
        @"#include <metal_stdlib>\nusing namespace metal;\n"
        @"struct VOut { float4 pos [[position]]; };\n"
        @"vertex VOut vs(uint vid [[vertex_id]]) {\n"
        @"  float2 p = float2(float(vid & 1), float((vid >> 1) & 1));\n"
        @"  VOut o; o.pos = float4(p.x == 0 ? -1.0 : 3.0, p.y == 0 ? -1.0 : 3.0, 0.5, 1.0); return o;\n"
        @"}\n"
        @"fragment float4 fs(VOut in [[stage_in]]) { return float4(0.0, 1.0, 0.0, 1.0); }\n"
        @"\n" options:nil error:err];
    if (!lib) return nil;
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = fmt;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    return [dev newRenderPipelineStateWithDescriptor:pd error:err];
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        id<MTLRenderPipelineState> pso = makePSO(dev, MTLPixelFormatBGRA8Unorm, &err);
        if (!pso) { printf("pso fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        id<MTLCommandQueue> q = [dev newCommandQueue];

        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> color = [dev newTextureWithDescriptor:td];
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:64 height:64 mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> depth = [dev newTextureWithDescriptor:dd];

        // Case A: no depth bounds -> quad (depth 0.5 < 1.0) passes
        // Case B: bounds [0.8, 1.0] -> quad depth 0.5 outside -> discarded
        for (int bounds = 0; bounds <= 1; bounds++) {
            id<MTLCommandBuffer> cb = [q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = color;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.colorAttachments[0].clearColor = MTLClearColorMake(1, 0, 0, 1); // red
            rpd.depthAttachment.texture = depth;
            rpd.depthAttachment.loadAction = MTLLoadActionClear;
            rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
            rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];
            MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
            dsd.depthCompareFunction = MTLCompareFunctionLess;
            dsd.depthWriteEnabled = YES;
            id<MTLDepthStencilState> ds = [dev newDepthStencilStateWithDescriptor:dsd];
            [enc setDepthStencilState:ds];
            if (bounds) [enc setDepthTestMinBound:0.8f maxBound:1.0f];
            [enc setRenderPipelineState:pso];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            uint8_t px[4];
            [color getBytes:px bytesPerRow:4 fromRegion:MTLRegionMake2D(32, 32, 1, 1) mipmapLevel:0];
            fflush(stdout);
            printf("case %s: pixel = %02x%02x%02x -> %s\n",
                   bounds ? "bounds[0.8,1.0]" : "no bounds",
                   px[2], px[1], px[0],
                   (px[2] > 200 && px[1] < 50 && px[0] < 50) ? "RED (quad DISCARDED - depth bounds WORKING)" :
                   (px[2] < 50 && px[1] > 200 && px[0] < 50) ? "GREEN (quad drawn - bounds IGNORED)" : "UNKNOWN");
            fflush(stdout);
        }
    }
    return 0;
}
