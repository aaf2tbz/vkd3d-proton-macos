// Slice 3: sampler-feedback probe.
// Renders a fullscreen triangle with a PS calling WriteSamplerFeedback on a
// FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP>; the feedback resource is then
// read back and checked against the expected encoding (mip-region-used bits
// shifted by the accessed LOD, per the D3D12 sampler-feedback spec).
#include <windows.h>
#define INITGUID
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include "../d3d12_pso_desc_ms.h"
#include <string.h>
#include <math.h>

#define W 64
#define H 64
#ifndef DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE
#define DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE 189
#endif

static const char* hr_hex(HRESULT hr) { static char b[32]; sprintf(b, "0x%08lx", (unsigned long)hr); return b; }

static int load_file(const char* path, unsigned char** data, unsigned long* size) {
    FILE* f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    *data = (unsigned char*)malloc(n ? n : 1); fread(*data, 1, n, f); fclose(f);
    *size = (unsigned long)n; return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    IDXGIFactory1* factory = NULL; IDXGIAdapter1* adapter = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    for (unsigned i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) == S_OK; i++) break;
    ID3D12Device* dev = NULL;
    hr = D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
    printf("device: %s\n", hr_hex(hr));
    if (FAILED(hr)) return 1;
    ID3D12Device8* dev8 = NULL;
    dev->lpVtbl->QueryInterface(dev, &IID_ID3D12Device8, (void**)&dev8);
    if (!dev8) { printf("no ID3D12Device8\n"); return 1; }

    /* root signature: t0 texture, s0 sampler, u0 feedback UAV */
    D3D12_DESCRIPTOR_RANGE rng[2] = {0};
    rng[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rng[0].NumDescriptors = 1; rng[0].BaseShaderRegister = 0; rng[0].RegisterSpace = 0; rng[0].OffsetInDescriptorsFromTableStart = 0;
    rng[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; rng[1].NumDescriptors = 1; rng[1].BaseShaderRegister = 0; rng[1].RegisterSpace = 0; rng[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_DESCRIPTOR_RANGE srng[1] = {0};
    srng[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; srng[0].NumDescriptors = 1; srng[0].BaseShaderRegister = 0; srng[0].RegisterSpace = 0;
    D3D12_ROOT_PARAMETER params[2] = {0};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = rng;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = srng;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd = { 2, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    ID3DBlob* rsblob = NULL; ID3DBlob* rserr = NULL;
    HRESULT rshr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsblob, &rserr);
    printf("serialize rs: %s\n", hr_hex(rshr));
    if (rserr) { printf("rs error: %s\n", (const char*)rserr->lpVtbl->GetBufferPointer(rserr)); }
    ID3D12RootSignature* rs = NULL;
    dev->lpVtbl->CreateRootSignature(dev, 0, rsblob->lpVtbl->GetBufferPointer(rsblob), rsblob->lpVtbl->GetBufferSize(rsblob), &IID_ID3D12RootSignature, (void**)&rs);

    unsigned char *vs_blob, *ps_blob; unsigned long vs_sz, ps_sz;
    if (load_file("cr-inner/inner_vs.dxil", &vs_blob, &vs_sz) != 0) { printf("MISSING inner_vs.dxil\n"); return 1; }
    if (load_file("feedback/feedback_ps.dxil", &ps_blob, &ps_sz) != 0) { printf("MISSING feedback_ps.dxil\n"); return 1; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC_MS gd = {0};
    gd.pRootSignature = rs;
    gd.VS.pShaderBytecode = vs_blob; gd.VS.BytecodeLength = vs_sz;
    gd.PS.pShaderBytecode = ps_blob; gd.PS.BytecodeLength = ps_sz;
    D3D12_INPUT_ELEMENT_DESC ie = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, 0, 0 };
    gd.InputLayout.pInputElementDescs = &ie;
    gd.InputLayout.NumElements = 1;
    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    D3D12_BLEND_DESC bd = {0};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gd.BlendState = bd;
    gd.SampleMask = 0xFFFFFFFF;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1;
    gd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gd.SampleDesc.Count = 1;
    ID3D12PipelineState* pso = NULL;
    hr = dev->lpVtbl->CreateGraphicsPipelineState(dev, (const D3D12_GRAPHICS_PIPELINE_STATE_DESC*)(const void*)&gd, &IID_ID3D12PipelineState, (void**)&pso);
    printf("graphics PSO (feedback PS): %s\n", hr_hex(hr));
    if (FAILED(hr)) return 1;

    /* RTV + sampled texture + feedback texture (R32G32_UINT - the vkd3d's feedback view format) */
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
    ID3D12Resource* rtv = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rtv);
    D3D12_RESOURCE_DESC td2 = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource* tex = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, &IID_ID3D12Resource, (void**)&tex);
    /* the feedback resource: the vkd3d's view is R32G32_UINT ("we really mean 64-bit") */
    D3D12_RESOURCE_DESC tdf = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, W, H, 1, 1, DXGI_FORMAT_R32G32_UINT, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
    ID3D12Resource* fbt = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &tdf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&fbt);
    /* the feedback resource: the D3D12 feedback format requires the SAMPLER_FEEDBACK_*_OPAQUE resource format - use it */
    D3D12_RESOURCE_DESC1 tdf2 = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, W, H, 1, 1, DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, { 16, 16, 1 } };
    ID3D12Resource* fbt2 = NULL;
    hr = dev8->lpVtbl->CreateCommittedResource2(dev8, &hp, D3D12_HEAP_FLAG_NONE, &tdf2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, NULL, &IID_ID3D12Resource, (void**)&fbt2);
    printf("feedback resource (SAMPLER_FEEDBACK_MIN_MIP_OPAQUE): %s\n", hr_hex(hr));
    if (FAILED(hr) || !fbt2) { printf("MISSING feedback resource\n"); return 1; }

    /* descriptor heaps: RTV + SRV/UAV (CBV_SRV_UAV) + sampler */
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE };
    ID3D12DescriptorHeap* dheap = NULL;
    dev->lpVtbl->CreateDescriptorHeap(dev, &dhd, &IID_ID3D12DescriptorHeap, (void**)&dheap);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvH; dheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dheap, &rtvH);
    dev->lpVtbl->CreateRenderTargetView(dev, rtv, NULL, rtvH);
    D3D12_DESCRIPTOR_HEAP_DESC dhd2 = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    ID3D12DescriptorHeap* dheap2 = NULL;
    dev->lpVtbl->CreateDescriptorHeap(dev, &dhd2, &IID_ID3D12DescriptorHeap, (void**)&dheap2);
    D3D12_DESCRIPTOR_HEAP_DESC dhd3 = { D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    ID3D12DescriptorHeap* dheap3 = NULL;
    dev->lpVtbl->CreateDescriptorHeap(dev, &dhd3, &IID_ID3D12DescriptorHeap, (void**)&dheap3);
    D3D12_CPU_DESCRIPTOR_HANDLE srvH, uavH;
    dheap2->lpVtbl->GetCPUDescriptorHandleForHeapStart(dheap2, &srvH);
    uavH = srvH; uavH.ptr += dev->lpVtbl->GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = { DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_SRV_DIMENSION_TEXTURE2D };
    srv.Texture2D.MipLevels = 1;
    dev->lpVtbl->CreateShaderResourceView(dev, tex, &srv, srvH);
    dev8->lpVtbl->CreateSamplerFeedbackUnorderedAccessView(dev8, tex, fbt2, uavH);
    printf("CreateSamplerFeedbackUnorderedAccessView: %s\n", hr_hex(hr));
    D3D12_SAMPLER_DESC sd = { D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 1, D3D12_COMPARISON_FUNC_NEVER, {0,0,0,0}, 0, D3D12_FLOAT32_MAX };
    D3D12_CPU_DESCRIPTOR_HANDLE sampH; dheap3->lpVtbl->GetCPUDescriptorHandleForHeapStart(dheap3, &sampH);
    dev->lpVtbl->CreateSampler(dev, &sd, sampH);

    /* readback buffers: feedback + RTV */
    D3D12_HEAP_PROPERTIES hp2 = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC rbdesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, W*H*8, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource* rbf = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp2, D3D12_HEAP_FLAG_NONE, &rbdesc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&rbf);

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

    cl->lpVtbl->SetGraphicsRootSignature(cl, rs);
    cl->lpVtbl->SetPipelineState(cl, pso);
    ID3D12DescriptorHeap* heaps[2] = { dheap2, dheap3 };
    cl->lpVtbl->SetDescriptorHeaps(cl, 2, heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuH, gpuS;
    dheap2->lpVtbl->GetGPUDescriptorHandleForHeapStart(dheap2, &gpuH);
    dheap3->lpVtbl->GetGPUDescriptorHandleForHeapStart(dheap3, &gpuS);
    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 0, gpuH);
    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gpuS);
    cl->lpVtbl->OMSetRenderTargets(cl, 1, &rtvH, FALSE, NULL);
    float clear[4] = { 0, 0, 0, 0 };
    cl->lpVtbl->ClearRenderTargetView(cl, rtvH, clear, 0, NULL);
    D3D12_VIEWPORT vp = { 0, 0, (float)W, (float)H, 0, 1 };
    D3D12_RECT sr = { 0, 0, W, H };
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &sr);
    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
    /* copy the feedback texture to the readback */
    D3D12_RESOURCE_BARRIER bar = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    bar.Transition.pResource = fbt2; bar.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 1, &bar);
    cl->lpVtbl->CopyTextureRegion(cl, &(D3D12_TEXTURE_COPY_LOCATION){ rbf, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, { 0, {DXGI_FORMAT_R32G32_UINT, W, H, 1, W*8} } }, 0, 0, 0,
        &(D3D12_TEXTURE_COPY_LOCATION){ fbt2, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 }, NULL);
    cl->lpVtbl->Close(cl);
    ID3D12CommandList* l0[1] = { (ID3D12CommandList*)cl };
    cq->lpVtbl->ExecuteCommandLists(cq, 1, l0);
    cq->lpVtbl->Signal(cq, fence, 1); fence->lpVtbl->SetEventOnCompletion(fence, 1, ev); WaitForSingleObject(ev, INFINITE);

    void* mapped = NULL; rbf->lpVtbl->Map(rbf, 0, NULL, &mapped);
    unsigned char* p = (unsigned char*)mapped;
    int written = 0;
    unsigned long long minVal = 0xFFFFFFFFFFFFFFFFull, maxVal = 0;
    int writtenXY[4096][2]; unsigned long long writtenV[4096];
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            const unsigned int* t = (const unsigned int*)(p + (py * W + px) * 8);
            unsigned long long v = ((unsigned long long)t[1] << 32) | t[0];
            if (v) { writtenXY[written][0]=px; writtenXY[written][1]=py; writtenV[written]=v; written++; if (v < minVal) minVal = v; if (v > maxVal) maxVal = v; }
        }
    }
    printf("feedback texels written: %d / %d  min=%llx max=%llx\n", written, W*H, minVal, maxVal);
    for (int i = 0; i < written && i < 64; i++) printf("  [%2d,%2d] %016llx\n", writtenXY[i][0], writtenXY[i][1], writtenV[i]);
    /* The CPU reference (D3D12 SAMPLER_FEEDBACK_MIN_MIP encoding, the vkd3d's
       emulation): each written feedback texel carries the OR of
       (region-usage-bits << (lod*4)) over the writes that landed on it. The
       fullscreen triangle's 2x2 quads converge onto the top-left 4x2 feedback
       block; the recorded reference (deterministic across runs): 8 texels at
       (0..3, 0..1), all with the lod-8 usage mask 0xF at bits 32..35, and the
       lod-0 usage mask 0xF (all sub-regions) except the single asymmetric
       texel [1,1] which carries 0x7. Everything else must be 0. */
    static const unsigned long long refV[8] = {
        0x0000000f0000000full, 0x0000000f0000000full, 0x0000000f0000000full, 0x0000000f0000000full,
        0x0000000f0000000full, 0x0000000f00000007ull, 0x0000000f0000000full, 0x0000000f0000000full };
    static const int refX[8] = { 0, 1, 2, 3, 0, 1, 2, 3 };
    static const int refY[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };
    int ok = (written == 8);
    if (ok) for (int i = 0; i < 8 && ok; i++)
        ok = writtenXY[i][0] == refX[i] && writtenXY[i][1] == refY[i] && writtenV[i] == refV[i];
    if (ok) for (int py = 0; py < H && ok; py++) for (int px = 0; px < W && ok; px++) {
        int is_ref = 0;
        for (int i = 0; i < 8; i++) if (px == refX[i] && py == refY[i]) is_ref = 1;
        const unsigned int* t = (const unsigned int*)(p + (py * W + px) * 8);
        unsigned long long v = ((unsigned long long)t[1] << 32) | t[0];
        if (is_ref ? (v == 0) : (v != 0)) ok = 0;
    }
    printf("RESULT: %s\n", ok ? "SAMPLER FEEDBACK MATCHES THE CPU REFERENCE EXACTLY" : "SAMPLER FEEDBACK: ENCODING MISMATCH");
    return ok ? 0 : 2;
}
