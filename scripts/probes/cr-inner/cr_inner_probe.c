// Slice 4: CR tier-3 InnerCoverage probe.
// Renders a fullscreen triangle with conservative rasterization ON and a PS
// reading SV_InnerCoverage: fully-covered pixels -> red, others -> blue.
// The CPU reference: a pixel is fully covered iff all 4 of its corners are
// inside the ORIGINAL (pre-snap) triangle.
#include <windows.h>
#define INITGUID
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define W 64
#define H 64

static const char* hr_hex(HRESULT hr) { static char b[32]; sprintf(b, "0x%08lx", (unsigned long)hr); return b; }

static int load_file(const char* path, unsigned char** data, unsigned long* size) {
    FILE* f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    *data = (unsigned char*)malloc(n ? n : 1); fread(*data, 1, n, f); fclose(f);
    *size = (unsigned long)n; return 0;
}

static int compile_dxil(const char* hlsl, const char* target, const char* entry, ID3DBlob** out) {
    /* compile via the wine-hosted dxc (scripts/dxc.sh) - write the hlsl to a temp
     * file and invoke dxc through a helper exe is complex; instead the probe
     * expects pre-compiled .dxil files next to it (built by the build script). */
    (void)hlsl; (void)target; (void)entry; (void)out;
    return -1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* fullscreen triangle in NDC (covers the whole viewport) */
    /* inner coverage reference: corners of each pixel inside the ORIGINAL triangle */
    /* original triangle: the fullscreen triangle - (-1,-1),(3,-1),(-1,3) in NDC
     * -> in pixel coords with the D3D viewport transform: the triangle spans the
     * entire viewport, so EVERY pixel is fully covered EXCEPT the edge pixels
     * along the hypotenuse (x + y >= W+H-2 line) whose outer corners fall outside. */
    int ref_covered[W * H];
    int fc = 0;
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            /* the fullscreen triangle's edge in pixel space: the line from
             * (W-0.5, -0.5) to (-0.5, H-0.5): x/W + y/H = 1 (approx); the pixel
             * is fully covered iff its top-right corner (px+0.5-0.5+1, ...) is
             * inside: corner (px+1, py+1) satisfies (px+1)/W + (py+1)/H <= 1 */
            int covered = ((px + 1) * H + (py + 1) * W) <= W * H;
            ref_covered[py * W + px] = covered;
            fc += covered;
        }
    }

    IDXGIFactory1* factory = NULL; IDXGIAdapter1* adapter = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    for (unsigned i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) == S_OK; i++) break;
    ID3D12Device* dev = NULL;
    hr = D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
    printf("device: %s\n", hr_hex(hr));
    if (FAILED(hr)) return 1;
    D3D12_FEATURE_DATA_D3D12_OPTIONS opts;
    memset(&opts, 0, sizeof(opts));
    hr = dev->lpVtbl->CheckFeatureSupport(dev, (D3D12_FEATURE)0, &opts, sizeof(opts));
    printf("CR tier: %u (hr=%s)\n", (unsigned)opts.ConservativeRasterizationTier, hr_hex(hr));

    D3D12_ROOT_SIGNATURE_DESC rsd = { 0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    ID3DBlob* rsblob = NULL; ID3DBlob* rserr = NULL;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsblob, &rserr);
    ID3D12RootSignature* rs = NULL;
    dev->lpVtbl->CreateRootSignature(dev, 0, rsblob->lpVtbl->GetBufferPointer(rsblob), rsblob->lpVtbl->GetBufferSize(rsblob), &IID_ID3D12RootSignature, (void**)&rs);

    /* load the VS + PS from the .dxil files (compiled by the build script) */
    unsigned char *vs_blob, *ps_blob; unsigned long vs_sz, ps_sz;
    if (load_file("cr-inner/inner_vs.dxil", &vs_blob, &vs_sz) != 0) { printf("MISSING cr-inner/inner_vs.dxil\n"); return 1; }
    if (load_file("cr-inner/inner_ps.dxil", &ps_blob, &ps_sz) != 0) { printf("MISSING cr-inner/inner_ps.dxil\n"); return 1; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {0};
    gd.pRootSignature = rs;
    gd.VS.pShaderBytecode = vs_blob; gd.VS.BytecodeLength = vs_sz;
    gd.PS.pShaderBytecode = ps_blob; gd.PS.BytecodeLength = ps_sz;
    D3D12_INPUT_ELEMENT_DESC ie = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, 0, 0 };
    gd.InputLayout.pInputElementDescs = &ie;
    gd.InputLayout.NumElements = 1;
    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gd.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
    D3D12_BLEND_DESC bd = {0};
    gd.BlendState = bd;
    gd.SampleMask = 0xFFFFFFFF;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1;
    gd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gd.SampleDesc.Count = 1;
    ID3D12PipelineState* pso = NULL;
    hr = dev->lpVtbl->CreateGraphicsPipelineState(dev, &gd, &IID_ID3D12PipelineState, (void**)&pso);
    printf("graphics PSO (CR on + InnerCoverage PS): %s\n", hr_hex(hr));
    if (FAILED(hr)) return 1;

    /* the RTV texture */
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
    ID3D12Resource* rtv = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rtv);
    D3D12_HEAP_PROPERTIES hp2 = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC bd2 = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, W*H*4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource* rb = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp2, D3D12_HEAP_FLAG_NONE, &bd2, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&rb);
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE };
    ID3D12DescriptorHeap* dheap = NULL;
    dev->lpVtbl->CreateDescriptorHeap(dev, &dhd, &IID_ID3D12DescriptorHeap, (void**)&dheap);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvH; dheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dheap, &rtvH);
    dev->lpVtbl->CreateRenderTargetView(dev, rtv, NULL, rtvH);

    ID3D12CommandAllocator* ca = NULL;
    dev->lpVtbl->CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&ca);
    ID3D12GraphicsCommandList* cl = NULL;
    dev->lpVtbl->CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, NULL, &IID_ID3D12GraphicsCommandList, (void**)&cl);
    D3D12_COMMAND_QUEUE_DESC cqd = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 0 };
    ID3D12CommandQueue* cq = NULL;
    dev->lpVtbl->CreateCommandQueue(dev, &cqd, &IID_ID3D12CommandQueue, (void**)&cq);
    ID3D12Fence* fence = NULL;
    dev->lpVtbl->CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);

    D3D12_RESOURCE_BARRIER bar0 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    bar0.Transition.pResource = rtv; bar0.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; bar0.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 1, &bar0);
    cl->lpVtbl->CopyTextureRegion(cl, &(D3D12_TEXTURE_COPY_LOCATION){ rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, { 0, {DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, W*4} } }, 0, 0, 0,
        &(D3D12_TEXTURE_COPY_LOCATION){ rtv, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 }, NULL);
    cl->lpVtbl->Close(cl);
    ID3D12CommandList* l0[1] = { (ID3D12CommandList*)cl };
    cq->lpVtbl->ExecuteCommandLists(cq, 1, l0);
    cq->lpVtbl->Signal(cq, fence, 1); fence->lpVtbl->SetEventOnCompletion(fence, 1, ev); WaitForSingleObject(ev, INFINITE);

    /* the render: fullscreen triangle + the CR + the InnerCoverage PS */
    ca->lpVtbl->Reset(ca);
    cl->lpVtbl->Reset(cl, ca, NULL);
    cl->lpVtbl->SetGraphicsRootSignature(cl, rs);
    cl->lpVtbl->SetPipelineState(cl, pso);
    cl->lpVtbl->OMSetRenderTargets(cl, 1, &rtvH, FALSE, NULL);
    float clear[4] = { 0, 0, 1, 1 };
    cl->lpVtbl->ClearRenderTargetView(cl, rtvH, clear, 0, NULL);
    D3D12_VIEWPORT vp = { 0, 0, (float)W, (float)H, 0, 1 };
    D3D12_RECT sr = { 0, 0, W, H };
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &sr);
    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    /* the fullscreen triangle vertex buffer (NDC) */
    float vb_data[6] = { -1, -1,  3, -1,  -1, 3 };
    D3D12_HEAP_PROPERTIES hp3 = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC bd3 = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 6*4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource* vb = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp3, D3D12_HEAP_FLAG_NONE, &bd3, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&vb);
    void* vbm = NULL; vb->lpVtbl->Map(vb, 0, NULL, &vbm);
    memcpy(vbm, vb_data, sizeof(vb_data));
    vb->lpVtbl->Unmap(vb, 0, NULL);
    D3D12_VERTEX_BUFFER_VIEW vbv = { vb->lpVtbl->GetGPUVirtualAddress(vb), 6*4, 2*4 };
    cl->lpVtbl->IASetVertexBuffers(cl, 0, 1, &vbv);
    cl->lpVtbl->DrawInstanced(cl, 3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER bar = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    bar.Transition.pResource = rtv; bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 1, &bar);
    cl->lpVtbl->CopyTextureRegion(cl, &(D3D12_TEXTURE_COPY_LOCATION){ rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, { 0, {DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, W*4} } }, 0, 0, 0,
        &(D3D12_TEXTURE_COPY_LOCATION){ rtv, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 }, NULL);
    cl->lpVtbl->Close(cl);
    ID3D12CommandList* l1[1] = { (ID3D12CommandList*)cl };
    cq->lpVtbl->ExecuteCommandLists(cq, 1, l1);
    cq->lpVtbl->Signal(cq, fence, 2); fence->lpVtbl->SetEventOnCompletion(fence, 2, ev); WaitForSingleObject(ev, INFINITE);

    void* mapped = NULL; rb->lpVtbl->Map(rb, 0, NULL, &mapped);
    unsigned char* p = (unsigned char*)mapped;
    int mismatches = 0, red = 0, blue = 0;
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            const unsigned char* t = p + (py * W + px) * 4;
            int isRed = t[0] > 128 && t[1] < 128;
            if (isRed) red++;
            else blue++;
            if (isRed != ref_covered[py * W + px]) mismatches++;
        }
    }
    printf("first row: ");
    for (int px = 0; px < 16; px++) {
        const unsigned char* t = p + (0 * W + px) * 4;
        printf("%d/%d/%d ", t[0], t[1], t[2]);
    }
    printf("\n");
    printf("last row: ");
    for (int px = 0; px < 16; px++) {
        const unsigned char* t = p + ((H-1) * W + px) * 4;
        printf("%d/%d/%d ", t[0], t[1], t[2]);
    }
    printf("\n");
    printf("red=%d blue=%d expected_fully_covered=%d mismatches=%d\n", red, blue, fc, mismatches);
    printf("RESULT: %s\n", mismatches == 0 ? "CR TIER 3 INNERCOVERAGE WORKS" : "INNERCOVERAGE MISMATCH");
    return mismatches == 0 ? 0 : 2;
}
