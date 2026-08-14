// Pure-Metal test of the EXACT MVK-injected CR shaders (vertex expansion +
// swap-compensated position + fragment post-snap test) with the tri0
// geometry, to isolate the rasterizer behavior from the MVK pipeline path.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;

        // tri0 vertex data in NDC (the app's VB): (51,45),(38,7),(51,39) y-up
        float vdata[6] = { 51.0f/32.0f-1.0f, 45.0f/32.0f-1.0f,
                           38.0f/32.0f-1.0f, 7.0f/32.0f-1.0f,
                           51.0f/32.0f-1.0f, 39.0f/32.0f-1.0f };

        MTLRenderPipelineDescriptor *rpd = [MTLRenderPipelineDescriptor new];
        rpd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@R"MSL(
#include <metal_stdlib>
using namespace metal;
struct main0_out
{
    float4 gl_Position [[position]];
    float2 _mvkCROrigA [[flat]];
    float2 _mvkCROrigB [[flat]];
    float2 _mvkCROrigC [[flat]];
};
struct main0_in
{
    float2 pos [[attribute(0)]];
};
struct main0_in2
{
    float2 _mvkCROrigA [[flat]];
    float2 _mvkCROrigB [[flat]];
    float2 _mvkCROrigC [[flat]];
};

static float2 _mvkCRNDC2Pix(float2 _mvkNdc, float4 _mvkCRConst) {
    return float2((_mvkNdc.x * 0.5f + 0.5f) * _mvkCRConst.z + _mvkCRConst.x,
                  (_mvkCRConst.w * 0.5f - 0.5f * _mvkNdc.y * _mvkCRConst.w) - 1.0f + _mvkCRConst.y);
}
static float2 _mvkCRPix2NDC(float2 _mvkPix, float4 _mvkCRConst) {
    return float2((_mvkPix.x - _mvkCRConst.x) / _mvkCRConst.z * 2.0f - 1.0f,
                  1.0f - (_mvkPix.y - _mvkCRConst.y + 1.0f) / _mvkCRConst.w * 2.0f);
}
static float2 _mvkCRPosFetch(uint _mvkIdx, device const float4& _mvkCRPosInfo, device const float2* _mvkCRPosBuf) {
    uint _mvkOff = _mvkIdx * (uint)_mvkCRPosInfo.x + (uint)_mvkCRPosInfo.y;
    return *(const device float2*)((const device char*)_mvkCRPosBuf + _mvkOff);
}

vertex main0_out main0(main0_in in [[stage_in]], uint _mvkCRVID [[vertex_id]],
                       device const float4& _mvkCRConst [[buffer(1)]],
                       device const float4& _mvkCRPosInfo [[buffer(2)]],
                       device const float2* _mvkCRPosBuf [[buffer(3)]])
{
    uint _mvkCRVi = _mvkCRVID % 3u;
    uint _mvkCRTriBase = _mvkCRVID - _mvkCRVi;
    float2 _mvkCRSa = _mvkCRNDC2Pix(_mvkCRPosFetch(_mvkCRTriBase + 0u, _mvkCRPosInfo, _mvkCRPosBuf), _mvkCRConst);
    float2 _mvkCRSb = _mvkCRNDC2Pix(_mvkCRPosFetch(_mvkCRTriBase + 1u, _mvkCRPosInfo, _mvkCRPosBuf), _mvkCRConst);
    float2 _mvkCRSc = _mvkCRNDC2Pix(_mvkCRPosFetch(_mvkCRTriBase + 2u, _mvkCRPosInfo, _mvkCRPosBuf), _mvkCRConst);
    float2 _mvkCRPv = _mvkCRVi == 0u ? _mvkCRSa : (_mvkCRVi == 1u ? _mvkCRSb : _mvkCRSc);
    float2 _mvkCRQ1 = _mvkCRVi == 0u ? _mvkCRSb : (_mvkCRVi == 1u ? _mvkCRSc : _mvkCRSa);
    float2 _mvkCRQ2 = _mvkCRVi == 0u ? _mvkCRSc : (_mvkCRVi == 1u ? _mvkCRSa : _mvkCRSb);
    float2 _mvkCRE1 = normalize(_mvkCRQ1 - _mvkCRPv);
    float2 _mvkCRE2 = normalize(_mvkCRQ2 - _mvkCRPv);
    float2 _mvkCRDir = -normalize(_mvkCRE1 + _mvkCRE2);
    float _mvkCRCos = dot(_mvkCRE1, _mvkCRE2);
    float _mvkCRSinHalf = sqrt(max(0.0, (1.0 - _mvkCRCos) * 0.5));
    float _mvkCRDist = (0.70710678f * 2.0f) / max(_mvkCRSinHalf, 0.05f);
    float2 _mvkCRS = _mvkCRNDC2Pix(_mvkCRPosFetch(_mvkCRVID, _mvkCRPosInfo, _mvkCRPosBuf), _mvkCRConst);
    float _mvkCRDistX = _mvkCRDir.x > 0.0 ? (63.5 - _mvkCRS.x) / max(_mvkCRDir.x, 1e-4) : (_mvkCRS.x - 0.5) / max(-_mvkCRDir.x, 1e-4);
    float _mvkCRDistY = _mvkCRDir.y > 0.0 ? (63.5 - _mvkCRS.y) / max(_mvkCRDir.y, 1e-4) : (_mvkCRS.y - 0.5) / max(-_mvkCRDir.y, 1e-4);
    _mvkCRDist = min(_mvkCRDist, min(_mvkCRDistX, _mvkCRDistY));
    float2 _mvkCRSExp = _mvkCRS + _mvkCRDir * _mvkCRDist;
    float2 _mvkCRExpNDC = _mvkCRPix2NDC(_mvkCRSExp, _mvkCRConst);

    main0_out out = {};
    // the ORIGINAL position expression would be float4(in.pos, 0.5, 1.0); we
    // replace it with the expanded, swap-compensated position:
    // pre-invert so the SPIRV-Cross-style y-invert line cancels out
    out.gl_Position = float4(_mvkCRExpNDC.x, -_mvkCRExpNDC.y, 0.5, 1.0);
    out.gl_Position.y = -(out.gl_Position.y);
    out._mvkCROrigA = _mvkCRSa;
    out._mvkCROrigB = _mvkCRSb;
    out._mvkCROrigC = _mvkCRSc;
    return out;
}
static bool _mvkCRCovered(float2 _mvkPos, float2 _mvkA, float2 _mvkB, float2 _mvkC) {
    float2 _mvkSa = round(_mvkA);
    float2 _mvkSb = round(_mvkB);
    float2 _mvkSc = round(_mvkC);
    float _mvkA2 = (_mvkSb.x - _mvkSa.x) * (_mvkSc.y - _mvkSa.y) - (_mvkSb.y - _mvkSa.y) * (_mvkSc.x - _mvkSa.x);
    bool _mvkCCW = _mvkA2 >= 0.0;
    float _mvkE1 = (_mvkSb.x - _mvkSa.x) * (_mvkPos.y - _mvkSa.y) - (_mvkSb.y - _mvkSa.y) * (_mvkPos.x - _mvkSa.x);
    float _mvkE2 = (_mvkSc.x - _mvkSb.x) * (_mvkPos.y - _mvkSb.y) - (_mvkSc.y - _mvkSb.y) * (_mvkPos.x - _mvkSb.x);
    float _mvkE3 = (_mvkSa.x - _mvkSc.x) * (_mvkPos.y - _mvkSc.y) - (_mvkSa.y - _mvkSc.y) * (_mvkPos.x - _mvkSc.x);
    if (_mvkCCW) { return _mvkE1 >= -1e-4 && _mvkE2 >= -1e-4 && _mvkE3 >= -1e-4; }
    return _mvkE1 <= 1e-4 && _mvkE2 <= 1e-4 && _mvkE3 <= 1e-4;
}
fragment float4 fs(float4 _mvkCRFragPos [[position]],
                   device const float4& _mvkCRConst [[buffer(1)]],
                   main0_in2 _mvkCRFSIn0 [[stage_in]])
{
    if ( !_mvkCRCovered(float2(_mvkCRFragPos.x, _mvkCRFragPos.y),
                         _mvkCRFSIn0._mvkCROrigA, _mvkCRFSIn0._mvkCROrigB, _mvkCRFSIn0._mvkCROrigC) ) { discard_fragment(); }
    return float4(1,0,0,1);
}
)MSL" options:nil error:&err];
        if (!lib) { printf("lib fail: %s\n", err.localizedDescription.UTF8String); return 1; }
        rpd.vertexFunction = [lib newFunctionWithName:@"main0"];
        rpd.fragmentFunction = [lib newFunctionWithName:@"fs"];
        // the stage-in struct has an attribute but we don't use it (the
        // injected fetch reads the buffer) - set a minimal descriptor
        rpd.vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
        rpd.vertexDescriptor.attributes[0].offset = 0;
        rpd.vertexDescriptor.attributes[0].bufferIndex = 0;
        rpd.vertexDescriptor.layouts[0].stride = 8;
        id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:rpd error:&err];
        if (!pso) { printf("pso fail: %s\n", err.localizedDescription.UTF8String); return 1; }

        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> rt = [dev newTextureWithDescriptor:td];
        // constants: vp (0,0,64,64), posInfo (stride 8, offset 0), posBuf
        id<MTLBuffer> cbuf = [dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
        float* cp = (float*)cbuf.contents;
        cp[0]=0; cp[1]=0; cp[2]=64; cp[3]=64;   // viewport
        cp[4]=8; cp[5]=0; cp[6]=0; cp[7]=0;     // posInfo
        id<MTLBuffer> vb = [dev newBufferWithLength:24 options:MTLResourceStorageModeShared];
        memcpy(vb.contents, vdata, 24);
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        MTLRenderPassDescriptor *rpd2 = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd2.colorAttachments[0].texture = rt;
        rpd2.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd2.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
        rpd2.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd2];
        [enc setRenderPipelineState:pso];
        [enc setVertexBytes:cp length:16 atIndex:1];
        [enc setVertexBytes:cp+4 length:16 atIndex:2];
        [enc setVertexBuffer:vb offset:0 atIndex:3];
        MTLViewport vp0 = {0,0,64,64,0,1};
        [enc setViewport:vp0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        [cb commit]; [cb waitUntilCompleted];

        id<MTLBuffer> rb = [dev newBufferWithLength:64*64*4 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb2 = [q commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb2 blitCommandEncoder];
        [blit copyFromTexture:rt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                   sourceSize:MTLSizeMake(64,64,1) toBuffer:rb destinationOffset:0
                   destinationBytesPerRow:64*4 destinationBytesPerImage:64*64*4];
        [blit endEncoding];
        [cb2 commit]; [cb2 waitUntilCompleted];
        uint32_t* px = (uint32_t*)rb.contents;
        int tot = 0;
        printf("pure-metal injected rasterized mask:\n");
        for (int y = 0; y < 64; y++) {
            char row[65];
            for (int x = 0; x < 64; x++) { row[x] = (px[y*64+x]&0xff) ? '#' : '.'; if (px[y*64+x]&0xff) tot++; }
            row[64]=0;
            printf("%2d %s\n", y, row);
        }
        printf("total: %d\n", tot);
        fflush(stdout);
    }
    return 0;
}
