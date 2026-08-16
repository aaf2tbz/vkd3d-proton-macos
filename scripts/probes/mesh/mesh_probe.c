// D3D12 mesh-shader acceptance probe (Slice 1 / M1.4).
// DispatchMesh over a fullscreen-triangle mesh shader; the render target
// readback must match a CPU barycentric reference exactly.
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

#include "../d3d12_pso_desc_ms.h"

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    IDXGIFactory1* factory = NULL; IDXGIAdapter1* adapter = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    for (unsigned i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) == S_OK; i++) break;
    ID3D12Device* dev = NULL;
    hr = D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void**)&dev);
    printf("device: %s\n", hr_hex(hr));
    if (FAILED(hr)) return 1;

    /* empty root signature (mesh shaders have no vertex input) */
    D3D12_ROOT_SIGNATURE_DESC rsd = { 0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3DBlob* rsblob = NULL; ID3DBlob* rserr = NULL;
    HRESULT rshr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsblob, &rserr);
    if (FAILED(rshr)) { printf("serialize rs: %s\n", hr_hex(rshr)); return 1; }
    ID3D12RootSignature* rs = NULL;
    dev->lpVtbl->CreateRootSignature(dev, 0, rsblob->lpVtbl->GetBufferPointer(rsblob), rsblob->lpVtbl->GetBufferSize(rsblob), &IID_ID3D12RootSignature, (void**)&rs);

    unsigned char *ms_blob, *ps_blob; unsigned long ms_sz, ps_sz;
    if (load_file("mesh/mesh_fullscreen.dxil", &ms_blob, &ms_sz) != 0) { printf("MISSING mesh_fullscreen.dxil\n"); return 1; }
    if (load_file("mesh/mesh_ps.dxil", &ps_blob, &ps_sz) != 0) { printf("MISSING mesh_ps.dxil\n"); return 1; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC_MS gd = {0};
    gd.pRootSignature = rs;
    gd.MS.pShaderBytecode = ms_blob; gd.MS.BytecodeLength = ms_sz;
    gd.PS.pShaderBytecode = ps_blob; gd.PS.BytecodeLength = ps_sz;
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
    printf("mesh PSO: %s\n", hr_hex(hr));
    if (FAILED(hr)) return 1;

    /* RTV */
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
    ID3D12Resource* rtv = NULL;
    dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rtv);
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE };
    ID3D12DescriptorHeap* dheap = NULL;
    dev->lpVtbl->CreateDescriptorHeap(dev, &dhd, &IID_ID3D12DescriptorHeap, (void**)&dheap);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvH; dheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dheap, &rtvH);
    dev->lpVtbl->CreateRenderTargetView(dev, rtv, NULL, rtvH);

    /* readback buffer */
    D3D12_HEAP_PROPERTIES hp2 = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC rbdesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, W*H*4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
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
    cl->lpVtbl->OMSetRenderTargets(cl, 1, &rtvH, FALSE, NULL);
    float clear[4] = { 0, 0, 0, 0 };
    cl->lpVtbl->ClearRenderTargetView(cl, rtvH, clear, 0, NULL);
    D3D12_VIEWPORT vp = { 0, 0, (float)W, (float)H, 0, 1 };
    D3D12_RECT sr = { 0, 0, W, H };
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &sr);
    ID3D12GraphicsCommandList6* cl6 = NULL;
    cl->lpVtbl->QueryInterface(cl, &IID_ID3D12GraphicsCommandList6, (void**)&cl6);
    if (!cl6) { printf("no ID3D12GraphicsCommandList6\n"); return 1; }
    cl6->lpVtbl->DispatchMesh(cl6, 1, 1, 1);
    cl->lpVtbl->Close(cl);
    ID3D12CommandList* l0[1] = { (ID3D12CommandList*)cl };
    cq->lpVtbl->ExecuteCommandLists(cq, 1, l0);
    cq->lpVtbl->Signal(cq, fence, 1); fence->lpVtbl->SetEventOnCompletion(fence, 1, ev); WaitForSingleObject(ev, INFINITE);

    /* copy RTV -> readback */
    ID3D12CommandAllocator* ca2 = NULL;
    dev->lpVtbl->CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&ca2);
    ID3D12GraphicsCommandList* cl2 = NULL;
    dev->lpVtbl->CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca2, NULL, &IID_ID3D12GraphicsCommandList, (void**)&cl2);
    D3D12_RESOURCE_BARRIER bar = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    bar.Transition.pResource = rtv; bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl2->lpVtbl->ResourceBarrier(cl2, 1, &bar);
    cl2->lpVtbl->CopyTextureRegion(cl2, &(D3D12_TEXTURE_COPY_LOCATION){ rbf, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, { 0, {DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, W*4} } }, 0, 0, 0,
        &(D3D12_TEXTURE_COPY_LOCATION){ rtv, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 }, NULL);
    cl2->lpVtbl->Close(cl2);
    ID3D12CommandList* l1[1] = { (ID3D12CommandList*)cl2 };
    cq->lpVtbl->ExecuteCommandLists(cq, 1, l1);
    cq->lpVtbl->Signal(cq, fence, 2); fence->lpVtbl->SetEventOnCompletion(fence, 2, ev); WaitForSingleObject(ev, INFINITE);

    /* CPU reference: the fullscreen triangle (-1,-1),(3,-1),(-1,3) with a
       CONSTANT RED output (see mesh_fullscreen.hlsl): every covered pixel
       must be exactly (255,0,0,255). */
    void* mapped = NULL; rbf->lpVtbl->Map(rbf, 0, NULL, &mapped);
    unsigned char* p = (unsigned char*)mapped;
    int frag = 0, exact = 1;
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            unsigned char* q = p + (py * W + px) * 4;
            unsigned v = q[0] | (q[1]<<8) | (q[2]<<16) | (q[3]<<24);
            if (v) frag++;
            if (v != 0xff0000ff) { exact = 0; if (frag <= 4) printf("  px[%d,%d] %08x\n", px, py, v); }
        }
    }
    printf("fragments=%d / %d\n", frag, W*H);
    int ok = (frag == W*H) && exact;
    printf("RESULT: %s\n", ok ? "MESH DISPATCH PIXEL-EXACT (D3D12 PATH)" : "MESH DISPATCH: MISMATCH");
    return ok ? 0 : 2;
}
