#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>

// Feasibility: MSL framebuffer fetch ([[color(0)]]) + compute src op dst with
// fixed-function blending disabled (programmable blending on Apple silicon).
int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
            @"struct VOut { float4 pos [[position]]; };\n"
            @"vertex VOut vs(uint vid [[vertex_id]]) {\n"
            @"  float2 p = float2(float(vid & 1), float((vid >> 1) & 1));\n"
            @"  VOut o; o.pos = float4(p.x == 0 ? -1.0 : 3.0, p.y == 0 ? -1.0 : 3.0, 0.0, 1.0);\n"
            @"  return o;\n"
            @"}\n"
            // src = (0.1, 0.2, 0.3, 0.4); dst read via framebuffer fetch; XOR-ish op:
            // result = fmod(src+dst, 1) would be wrong; use real logic op on 8-bit:
            // dst comes as half4 normalized; we operate on 8-bit channels:
            @"fragment half4 fs(VOut in [[stage_in]], half4 dst [[color(0)]]) {\n"
            @"  half4 src = half4(0.1, 0.2, 0.3, 0.4);\n"
            @"  // emulate D3D12 XOR on 8-bit: (src ^ dst) per channel\n"
            @"  uchar4 s = uchar4(src * 255.0h);\n"
            @"  uchar4 d = uchar4(dst * 255.0h);\n"
            @"  uchar4 r = s ^ d;\n"
            @"  return half4(r) / 255.0h;\n"
            @"}\n" options:nil error:&err];
        if (!lib) { printf("lib fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pd.colorAttachments[0].blendingEnabled = NO;   // fixed-function blending OFF
        id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!pso) { printf("pso fail: %s\n", err.localizedDescription.UTF8String); return 1; }

        id<MTLCommandQueue> q = [dev newCommandQueue];
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> color = [dev newTextureWithDescriptor:td];

        // seed dst: 0xAA in R (BGRA: R is byte 2); full texel = 0x00AA0000 (R=0xAA)
        uint32_t seed = 0x0000AA00; // BGRA8: B=0,G=0,R=0xAA,A=0 -> bytes 00 AA 00 00? careful
        // BGRA8 memory order: B,G,R,A bytes. 0x0000AA00 = B=0x00,G=0xAA,R=0x00,A=0x00 -> that's G.
        // want R=0xAA: bytes B=0,G=0,R=0xAA,A=0 -> value 0x00AA0000
        uint32_t dstVal = 0x00AA0000;
        uint32_t *rows = (uint32_t*)malloc(64 * 256);
        for (int i = 0; i < 64*64; i++) rows[i] = dstVal;
        [color replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:rows bytesPerRow:256];
        free(rows);
        // src.R = 0.1*255 = 26 (0x1A); XOR: 0xAA ^ 0x1A = 0xB0 (176); 176/255 = 0.69
        id<MTLCommandBuffer> cb = [q commandBuffer];
        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = color;
        rpd.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];
        [enc setRenderPipelineState:pso];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        uint8_t px[4];
        [color getBytes:px bytesPerRow:4 fromRegion:MTLRegionMake2D(32,32,1,1) mipmapLevel:0];
        // exact expectations from the shader: src = uchar4(0.1,0.2,0.3,0.4 * 255) truncated
        uint8_t eR = (uint8_t)(0.1f * 255.0f) ^ 0xAA;   // 25 ^ 0xAA = 0xB3
        uint8_t eG = (uint8_t)(0.2f * 255.0f) ^ 0x00;   // 51
        uint8_t eB = (uint8_t)(0.3f * 255.0f) ^ 0x00;   // 76
        uint8_t eA = (uint8_t)(0.4f * 255.0f) ^ 0x00;   // 102
        printf("pixel BGR = %02x %02x %02x (A=%02x)\n", px[0], px[1], px[2], px[3]);
        printf("expect   = %02x %02x %02x (A=%02x)  [src^dst, blending OFF]\n", eB, eG, eR, eA);
        printf("RESULT: %s\n",
               (px[2] == eR && px[1] == eG && px[0] == eB && px[3] == eA) ?
               "FRAMEBUFFER FETCH + LOGIC OP WORKS" : "FAILED");
        fflush(stdout);
    }
    return 0;
}
