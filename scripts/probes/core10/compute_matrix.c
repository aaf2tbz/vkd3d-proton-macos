// M13: compute-only matrix on a CORE_1_0 device: compute PSO + dispatch + UAV readback.
#include <windows.h>
#define INITGUID
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <string.h>

#define FL_1_0_CORE 0x1000



#ifndef IID_PPV_ARGS
#define IID_PPV_ARGS(ppType) __uuidof(**(ppType))
#endif

static const char* hr_hex(HRESULT hr) {
    static char buf[32];
    sprintf(buf, "0x%08lx", (unsigned long)hr);
    return buf;
}

// minimal DXIL: a compute shader that writes 42.0 to a UAV (hand-assembled DXIL container)
// We use the D3D12CreateDevice + ID3D12Device methods directly via the vtable.

int main(void) {
    IDXGIFactory1* factory = NULL;
    IDXGIAdapter1* adapter = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    if (FAILED(hr) || !factory) { printf("factory fail: %s\n", hr_hex(hr)); return 1; }
    for (unsigned i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->lpVtbl->GetDesc1(adapter, &desc);
        printf("adapter %u: vendor 0x%04x device 0x%04x\n", i, desc.VendorId, desc.DeviceId);
        break;
    }
    if (!adapter) { printf("no adapter\n"); return 1; }

    ID3D12Device* dev = NULL;
    hr = D3D12CreateDevice((IUnknown*)adapter, FL_1_0_CORE, &IID_ID3D12Device, (void**)&dev);
    printf("CORE device create: %s dev=%p\n", hr_hex(hr), (void*)dev);
    if (FAILED(hr) || !dev) { printf("RESULT: CORE DEVICE CREATE FAILED\n"); return 1; }

    // root signature: one UAV (descriptor table with one SRV/UAV range)
    D3D12_ROOT_PARAMETER param = { D3D12_ROOT_PARAMETER_TYPE_UAV, {0}, D3D12_SHADER_VISIBILITY_ALL };
    param.Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC rsdesc = { 1, &param, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3D12RootSignature* rs = NULL;
    ID3DBlob* rsblob = NULL;
    ID3DBlob* rserr = NULL;
    hr = D3D12SerializeRootSignature(&rsdesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsblob, &rserr);
    if (FAILED(hr)) { printf("serialize rs fail: %s\n", hr_hex(hr)); return 1; }
    hr = dev->lpVtbl->CreateRootSignature(dev, 0, rsblob->lpVtbl->GetBufferPointer(rsblob), rsblob->lpVtbl->GetBufferSize(rsblob), &IID_ID3D12RootSignature, (void**)&rs);
    if (FAILED(hr)) { printf("create rs fail: %s\n", hr_hex(hr)); return 1; }

    // a minimal DXIL blob: we can't easily hand-assemble DXIL; instead use a tiny compiled shader blob
    // via D3DCompile if available, else fail with a note.
    ID3DBlob* cs = NULL;
    {
        // try D3DCompile from d3dcompiler_47
        typedef HRESULT (WINAPI *D3DCompileFn)(const void*, SIZE_T, const char*, const void*, const void*, const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);
        HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
        if (!dc) { printf("no d3dcompiler_47\n"); return 1; }
        D3DCompileFn D3DCompileF = (D3DCompileFn)GetProcAddress(dc, "D3DCompile");
        const char* src =
            "RWStructuredBuffer<float> buf : register(u0);\n"
            "[numthreads(8,1,1)]\n"
            "void main(uint3 tid : SV_DispatchThreadID) { buf[tid.x] = 42.0; }\n";
        hr = D3DCompileF(src, strlen(src), "cs", NULL, NULL, "main", "cs_5_0", 0, 0, &cs, NULL);
        if (FAILED(hr) || !cs) { printf("compile cs fail: %s\n", hr_hex(hr)); return 1; }
        printf("compute shader compiled: %lu bytes\n", (unsigned long)cs->lpVtbl->GetBufferSize(cs));
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = {0};
    cpsd.pRootSignature = rs;
    cpsd.CS.pShaderBytecode = cs->lpVtbl->GetBufferPointer(cs);
    cpsd.CS.BytecodeLength = cs->lpVtbl->GetBufferSize(cs);
    ID3D12PipelineState* pso = NULL;
    hr = dev->lpVtbl->CreateComputePipelineState(dev, &cpsd, &IID_ID3D12PipelineState, (void**)&pso);
    if (FAILED(hr)) { printf("create pso fail: %s\n", hr_hex(hr)); return 1; }
    printf("compute PSO: OK\n");

    // the UAV buffer + the readback
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC bd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 64*4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
    ID3D12Resource* uav = NULL;
    hr = dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&uav);
    if (FAILED(hr)) { printf("uav create fail: %s\n", hr_hex(hr)); return 1; }
    D3D12_HEAP_PROPERTIES hp2 = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC bd2 = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 64*4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource* rb = NULL;
    hr = dev->lpVtbl->CreateCommittedResource(dev, &hp2, D3D12_HEAP_FLAG_NONE, &bd2, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&rb);
    if (FAILED(hr)) { printf("readback create fail: %s\n", hr_hex(hr)); return 1; }

    // descriptor heap + the UAV descriptor
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    ID3D12DescriptorHeap* dheap = NULL;
    hr = dev->lpVtbl->CreateDescriptorHeap(dev, &dhd, &IID_ID3D12DescriptorHeap, (void**)&dheap);
    if (FAILED(hr)) { printf("dheap fail: %s\n", hr_hex(hr)); return 1; }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = { DXGI_FORMAT_UNKNOWN, D3D12_UAV_DIMENSION_BUFFER };
    uavd.Buffer.NumElements = 16;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuH;
    dheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dheap, &cpuH);
    dev->lpVtbl->CreateUnorderedAccessView(dev, uav, NULL, &uavd, cpuH);

    // command allocator + list + queue
    ID3D12CommandAllocator* ca = NULL;
    hr = dev->lpVtbl->CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_COMPUTE, &IID_ID3D12CommandAllocator, (void**)&ca);
    if (FAILED(hr)) { printf("allocator fail: %s\n", hr_hex(hr)); return 1; }
    ID3D12GraphicsCommandList* cl = NULL;
    hr = dev->lpVtbl->CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, ca, NULL, &IID_ID3D12GraphicsCommandList, (void**)&cl);
    if (FAILED(hr)) { printf("cmdlist fail: %s\n", hr_hex(hr)); return 1; }
    ID3D12CommandQueue* cq = NULL;
    D3D12_COMMAND_QUEUE_DESC cqd = { D3D12_COMMAND_LIST_TYPE_COMPUTE, 0, 0, 0 };
    hr = dev->lpVtbl->CreateCommandQueue(dev, &cqd, &IID_ID3D12CommandQueue, (void**)&cq);
    if (FAILED(hr)) { printf("queue fail: %s\n", hr_hex(hr)); return 1; }

    /* initialize the UAV to 7.0 via an upload buffer (fence 1) */
    D3D12_HEAP_PROPERTIES hp3 = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC bd3 = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 64*4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource* up = NULL;
    hr = dev->lpVtbl->CreateCommittedResource(dev, &hp3, D3D12_HEAP_FLAG_NONE, &bd3, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&up);
    void* upm = NULL;
    up->lpVtbl->Map(up, 0, NULL, &upm);
    float* uf = (float*)upm;
    for (int i = 0; i < 64; i++) uf[i] = 7.0f;
    up->lpVtbl->Unmap(up, 0, NULL);
    ID3D12CommandAllocator* ca2 = NULL;
    dev->lpVtbl->CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_COMPUTE, &IID_ID3D12CommandAllocator, (void**)&ca2);
    ID3D12GraphicsCommandList* cl2 = NULL;
    dev->lpVtbl->CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, ca2, NULL, &IID_ID3D12GraphicsCommandList, (void**)&cl2);
    D3D12_RESOURCE_BARRIER b2 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    b2.Transition.pResource = uav;
    b2.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b2.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cl2->lpVtbl->ResourceBarrier(cl2, 1, &b2);
    cl2->lpVtbl->CopyBufferRegion(cl2, uav, 0, up, 0, 64*4);
    D3D12_RESOURCE_BARRIER b3 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    b3.Transition.pResource = uav;
    b3.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b3.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cl2->lpVtbl->ResourceBarrier(cl2, 1, &b3);
    cl2->lpVtbl->Close(cl2);
    ID3D12Fence* fence = NULL;
    dev->lpVtbl->CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    ID3D12CommandList* lists2[1] = { (ID3D12CommandList*)cl2 };
    cq->lpVtbl->ExecuteCommandLists(cq, 1, lists2);
    cq->lpVtbl->Signal(cq, fence, 1);
    fence->lpVtbl->SetEventOnCompletion(fence, 1, ev);
    WaitForSingleObject(ev, INFINITE);

    /* dispatch + readback copy in one list (fence 2) */
    cl->lpVtbl->SetComputeRootSignature(cl, rs);
    ID3D12DescriptorHeap* heaps[1] = { dheap };
    cl->lpVtbl->SetDescriptorHeaps(cl, 1, heaps);
    cl->lpVtbl->SetComputeRootUnorderedAccessView(cl, 0, uav->lpVtbl->GetGPUVirtualAddress(uav));
    cl->lpVtbl->SetPipelineState(cl, pso);
    cl->lpVtbl->Dispatch(cl, 8, 1, 1);
    D3D12_RESOURCE_BARRIER bar = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    bar.Transition.pResource = uav;
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 1, &bar);
    cl->lpVtbl->CopyBufferRegion(cl, rb, 0, uav, 0, 64*4);
    cl->lpVtbl->Close(cl);
    ID3D12CommandList* lists[1] = { (ID3D12CommandList*)cl };
    cq->lpVtbl->ExecuteCommandLists(cq, 1, lists);
    cq->lpVtbl->Signal(cq, fence, 2);
    fence->lpVtbl->SetEventOnCompletion(fence, 2, ev);
    WaitForSingleObject(ev, INFINITE);

    void* mapped = NULL;
    rb->lpVtbl->Map(rb, 0, NULL, &mapped);
    float* f = (float*)mapped;
    printf("UAV readback[0..3]: %f %f %f %f\n", f[0], f[1], f[2], f[3]);
    printf("RESULT: %s\n", f[0] == 42.0f ? "CORE_1_0 COMPUTE MATRIX WORKS" : (f[0] == 7.0f ? "KERNEL DID NOT RUN (7.0 preserved)" : "COMPUTE MATRIX FAILED"));
    return f[0] == 42.0f ? 0 : 2;
}
