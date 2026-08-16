// Plain-Metal mesh-shading baseline probe: tries every combination of the
// mesh draw semantics to find what actually produces pixels on this platform.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>

static const int W = 64, H = 64;

static int readback_count(id<MTLTexture> tex) {
    int n = W * H * 4;
    unsigned char *buf = (unsigned char*)malloc(n);
    [tex getBytes:buf bytesPerRow:W*4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
    int c = 0;
    for (int i = 0; i < n; i += 4) if (buf[i] || buf[i+1] || buf[i+2] || buf[i+3]) c++;
    free(buf);
    return c;
}

// The mesh kernel: 3 vertices (fullscreen triangle) + 1 primitive.
// SET_TG: call set_threadgroup_size(3) explicitly.
static NSString *meshSrc(void) {
    return @R"MSL(
#include <metal_stdlib>
using namespace metal;
struct MeshOut {
    float4 position [[position]];
    float4 color;
};
[[mesh]] void meshMain(mesh<MeshOut, void, 3, 1, topology::triangle> m,
                       uint3 pid [[thread_position_in_grid]])
{
    if (pid.x == 0) m.set_vertex(0, MeshOut{float4(-1.0, -1.0, 0.0, 1.0), float4(1, 0, 0, 1)});
    if (pid.x == 1) m.set_vertex(1, MeshOut{float4( 3.0, -1.0, 0.0, 1.0), float4(0, 1, 0, 1)});
    if (pid.x == 2) m.set_vertex(2, MeshOut{float4(-1.0,  3.0, 0.0, 1.0), float4(0, 0, 1, 1)});
    if (pid.x == 0) {
        m.set_index(0, 0);
        m.set_index(1, 1);
        m.set_index(2, 2);
        m.set_primitive_count(1);
    }
}
struct FragOut {
    float4 color [[color(0)]];
};
fragment FragOut fragMain(MeshOut in [[stage_in]]) {
    FragOut o; o.color = in.color; return o;
}
)MSL";
}

static NSString *objMeshSrc(void) {
    return @"#include <metal_stdlib>\nusing namespace metal;\n"
    "struct MeshOut {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "[[object]] void objMain(mesh_grid_properties spvMgp, uint3 gid [[threadgroups_per_grid]]) {\n"
    "    spvMgp.set_threadgroups_per_grid(uint3(1, 1, 1));\n"
    "}\n"
    "[[mesh]] void meshMain(mesh<MeshOut, void, 3, 1, topology::triangle> m,\n"
    "                       uint3 pid [[thread_position_in_grid]]) {\n"
    "    if (pid.x == 0) m.set_vertex(0, MeshOut{float4(-1.0, -1.0, 0.0, 1.0), float4(1, 0, 0, 1)});\n"
    "    if (pid.x == 1) m.set_vertex(1, MeshOut{float4( 3.0, -1.0, 0.0, 1.0), float4(0, 1, 0, 1)});\n"
    "    if (pid.x == 2) m.set_vertex(2, MeshOut{float4(-1.0,  3.0, 0.0, 1.0), float4(0, 0, 1, 1)});\n"
    "    if (pid.x == 0) { m.set_index(0, 0); m.set_index(1, 1); m.set_index(2, 2); m.set_primitive_count(1); }\n"
    "}\n"
    "struct FragOut { float4 color [[color(0)]]; };\n"
    "fragment FragOut fragMain(MeshOut in [[stage_in]]) { FragOut o; o.color = in.color; return o; }\n";
}

static void runVariant(id<MTLDevice> dev, NSString *name, id<MTLLibrary> lib,
                       NSString *meshFn, NSString *objFn, NSString *fragFn,
                       MTLSize objectTG, MTLSize meshTG, MTLSize grid) {
    NSError *err = nil;
    MTLMeshRenderPipelineDescriptor *mpd = [MTLMeshRenderPipelineDescriptor new];
    mpd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    mpd.meshFunction = [lib newFunctionWithName:meshFn];
    if (objFn) mpd.objectFunction = [lib newFunctionWithName:objFn];
    mpd.fragmentFunction = [lib newFunctionWithName:fragFn];
    mpd.maxTotalThreadsPerMeshThreadgroup = 1024;
    mpd.maxTotalThreadsPerObjectThreadgroup = objFn ? 1024 : 0;
    id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithMeshDescriptor:mpd options:0 reflection:nil error:&err];
    if (!pso) { printf("%-34s PIPELINE FAIL: %s\n", name.UTF8String, err.localizedDescription.UTF8String); return; }

    id<MTLCommandQueue> cq = [dev newCommandQueue];
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:W height:H mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> tex = [dev newTextureWithDescriptor:td];

    id<MTLCommandBuffer> cb = [cq commandBuffer];
    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = tex;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];
    [enc setCullMode:MTLCullModeNone];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];
    [enc setRenderPipelineState:pso];
    [enc drawMeshThreadgroups:grid
   threadsPerObjectThreadgroup:objectTG
     threadsPerMeshThreadgroup:meshTG];
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    int c = readback_count(tex);
    printf("%-34s %s (%d px)\n", name.UTF8String, c > 0 ? "PIXELS LAND" : "NO PIXELS", c);
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        printf("device: %s (maxThreadsPerTG %lu)\n", dev.name.UTF8String, (unsigned long)dev.maxThreadsPerThreadgroup.width);
        printf("mesh support: %s\n", [dev supportsFamily:MTLGPUFamilyApple9] ? "Apple9 YES" : "Apple9 NO");

        NSError *err = nil;
        MTLCompileOptions *copts = [MTLCompileOptions new];
        id<MTLLibrary> libMeshOnly = [dev newLibraryWithSource:meshSrc() options:copts error:&err];
        if (!libMeshOnly) { printf("compile fail: %s\n", err.localizedDescription.UTF8String); return 1; }

        MTLSize tg1 = MTLSizeMake(1, 1, 1);
        MTLSize tg3 = MTLSizeMake(3, 1, 1);
        MTLSize tg32 = MTLSizeMake(32, 1, 1);
        MTLSize tg64 = MTLSizeMake(64, 1, 1);
        MTLSize grid1 = MTLSizeMake(1, 1, 1);
        MTLSize grid2 = MTLSizeMake(2, 1, 1);

        // mesh-only, no set_threadgroup_size
        runVariant(dev, @"mesh-only tg=3 (no setTG)", libMeshOnly, @"meshMain", nil, @"fragMain", tg1, tg3, grid1);
        runVariant(dev, @"mesh-only tg=32 (no setTG)", libMeshOnly, @"meshMain", nil, @"fragMain", tg1, tg32, grid1);
        runVariant(dev, @"mesh-only tg=64 (no setTG)", libMeshOnly, @"meshMain", nil, @"fragMain", tg1, tg64, grid1);
        runVariant(dev, @"mesh-only tg=3 grid=2", libMeshOnly, @"meshMain", nil, @"fragMain", tg1, tg3, grid2);
        runVariant(dev, @"mesh-only tg=1", libMeshOnly, @"meshMain", nil, @"fragMain", tg1, tg1, grid1);
        NSError *oerr = nil;
        id<MTLLibrary> libObjMesh = [dev newLibraryWithSource:objMeshSrc() options:copts error:&oerr];
        if (libObjMesh) {
            runVariant(dev, @"object+mesh objTG=1 meshTG=3", libObjMesh, @"meshMain", @"objMain", @"fragMain", tg1, tg3, grid1);
            runVariant(dev, @"object+mesh objTG=1 meshTG=32", libObjMesh, @"meshMain", @"objMain", @"fragMain", tg1, tg32, grid1);
        } else {
            printf("object+mesh compile fail: %s\n", oerr.localizedDescription.UTF8String);
        }
        return 0;
    }
}
