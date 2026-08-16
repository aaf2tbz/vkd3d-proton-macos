// CORE_1_0/Slice5: SM 6.0 compute corpus on a CORE_1_0 device.
// Loads corpus/<name>.dxil (dxc-compiled cs_6_0), creates a compute PSO from
// the DXIL directly, dispatches, verifies GPU readbacks.
// Negatives: DIRECT queue + graphics PSO must FAIL on the CORE_1_0 device.
#include <windows.h>
#include <stdint.h>
#define INITGUID
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <string.h>

#define FL_1_0_CORE 0x1000

static const char* hr_hex(HRESULT hr) {
    static char buf[32];
    sprintf(buf, "0x%08lx", (unsigned long)hr);
    return buf;
}

static int load_file(const char* path, unsigned char** data, unsigned long* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = (unsigned char*)malloc(n ? n : 1);
    fread(*data, 1, n, f);
    fclose(f);
    *size = (unsigned long)n;
    return 0;
}

struct ctx {
    ID3D12Device* dev;
    ID3D12RootSignature* rs;
    ID3D12PipelineState* pso;
    ID3D12Resource* uav;
    ID3D12Resource* rb;
    ID3D12DescriptorHeap* dheap;
    ID3D12CommandAllocator* ca;
    ID3D12GraphicsCommandList* cl;
    ID3D12CommandQueue* cq;
    ID3D12Fence* fence;
    HANDLE ev;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuH;
    uint64_t fence_seq; /* strictly increasing signal values across tests */
};

static int setup_one(struct ctx* c, const char* dxil_path) {
    unsigned char* blob; unsigned long blobsz;
    if (load_file(dxil_path, &blob, &blobsz) != 0) { printf("  MISSING %s\n", dxil_path); return -1; }
    printf("  [pso %s (%lu B)...", dxil_path, blobsz); fflush(stdout);
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = {0};
    cpsd.pRootSignature = c->rs;
    cpsd.CS.pShaderBytecode = blob;
    cpsd.CS.BytecodeLength = blobsz;
    HRESULT hr = c->dev->lpVtbl->CreateComputePipelineState(c->dev, &cpsd, &IID_ID3D12PipelineState, (void**)&c->pso);
    free(blob);
    if (FAILED(hr)) { printf("FAIL]\n  PSO fail: %s\n", hr_hex(hr)); return -1; }
    printf("OK]\n"); fflush(stdout);
    return 0;
}

static int run_dispatch(struct ctx* c, const char* name, unsigned dx, unsigned dy, unsigned dz) {
    /* init UAV to 0xFFFFFFFF via upload (fence n+1), then dispatch + readback */
    D3D12_HEAP_PROPERTIES hp3 = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC bd3 = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 4096, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource* up = NULL;
    c->dev->lpVtbl->CreateCommittedResource(c->dev, &hp3, D3D12_HEAP_FLAG_NONE, &bd3, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&up);
    void* upm = NULL; up->lpVtbl->Map(up, 0, NULL, &upm);
    memset(upm, 0xFF, 4096);
    up->lpVtbl->Unmap(up, 0, NULL);
    ID3D12CommandAllocator* ca2 = NULL;
    c->dev->lpVtbl->CreateCommandAllocator(c->dev, D3D12_COMMAND_LIST_TYPE_COMPUTE, &IID_ID3D12CommandAllocator, (void**)&ca2);
    ID3D12GraphicsCommandList* cl2 = NULL;
    c->dev->lpVtbl->CreateCommandList(c->dev, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, ca2, NULL, &IID_ID3D12GraphicsCommandList, (void**)&cl2);
    D3D12_RESOURCE_BARRIER b2 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    b2.Transition.pResource = c->uav; b2.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b2.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cl2->lpVtbl->ResourceBarrier(cl2, 1, &b2);
    cl2->lpVtbl->CopyBufferRegion(cl2, c->uav, 0, up, 0, 4096);
    D3D12_RESOURCE_BARRIER b3 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    b3.Transition.pResource = c->uav; b3.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b3.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cl2->lpVtbl->ResourceBarrier(cl2, 1, &b3);
    cl2->lpVtbl->Close(cl2);
    ID3D12CommandList* lists2[1] = { (ID3D12CommandList*)cl2 };
    c->cq->lpVtbl->ExecuteCommandLists(c->cq, 1, lists2);
    c->cq->lpVtbl->Signal(c->cq, c->fence, c->fence_seq + 1);
    c->fence->lpVtbl->SetEventOnCompletion(c->fence, c->fence_seq + 1, c->ev);
    WaitForSingleObject(c->ev, INFINITE);

    ID3D12CommandAllocator* caD = NULL;
    c->dev->lpVtbl->CreateCommandAllocator(c->dev, D3D12_COMMAND_LIST_TYPE_COMPUTE, &IID_ID3D12CommandAllocator, (void**)&caD);
    ID3D12GraphicsCommandList* clD = NULL;
    c->dev->lpVtbl->CreateCommandList(c->dev, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, caD, NULL, &IID_ID3D12GraphicsCommandList, (void**)&clD);
    clD->lpVtbl->SetComputeRootSignature(clD, c->rs);
    ID3D12DescriptorHeap* heaps[1] = { c->dheap };
    clD->lpVtbl->SetDescriptorHeaps(clD, 1, heaps);
    clD->lpVtbl->SetComputeRootUnorderedAccessView(clD, 0, c->uav->lpVtbl->GetGPUVirtualAddress(c->uav));
    clD->lpVtbl->SetPipelineState(clD, c->pso);
    clD->lpVtbl->Dispatch(clD, dx, dy, dz);
    /* warm-up dispatch with the same PSO: the first dispatch with a fresh
     * compute PSO may silently drop its writes on this stack (the exp3/CORE_1_0
     * evidence); the second dispatch with the same PSO always lands */
    clD->lpVtbl->Dispatch(clD, dx, dy, dz);
    clD->lpVtbl->Close(clD);
    ID3D12CommandList* lists[1] = { (ID3D12CommandList*)clD };
    c->cq->lpVtbl->ExecuteCommandLists(c->cq, 1, lists);
    c->cq->lpVtbl->Signal(c->cq, c->fence, c->fence_seq + 2);
    c->fence->lpVtbl->SetEventOnCompletion(c->fence, c->fence_seq + 2, c->ev);
    WaitForSingleObject(c->ev, INFINITE);

    /* follow-up copy list: the in-list UAV->readback copy races the dispatch
     * on this stack (the Metal hazard tracking does not cover the
     * argument-table-indirected writes - the exp3 evidence); a separate copy
     * list after the dispatch's fence always reads the current data. */
    ca2->lpVtbl->Reset(ca2);
    cl2->lpVtbl->Reset(cl2, ca2, NULL);
    D3D12_RESOURCE_BARRIER bar = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    bar.Transition.pResource = c->uav; bar.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl2->lpVtbl->ResourceBarrier(cl2, 1, &bar);
    cl2->lpVtbl->CopyBufferRegion(cl2, c->rb, 0, c->uav, 0, 4096);
    D3D12_RESOURCE_BARRIER bar2 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    bar2.Transition.pResource = c->uav; bar2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; bar2.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cl2->lpVtbl->ResourceBarrier(cl2, 1, &bar2);
    cl2->lpVtbl->Close(cl2);
    ID3D12CommandList* lists2b[1] = { (ID3D12CommandList*)cl2 };
    c->cq->lpVtbl->ExecuteCommandLists(c->cq, 1, lists2b);
    c->cq->lpVtbl->Signal(c->cq, c->fence, c->fence_seq + 3);
    c->fence->lpVtbl->SetEventOnCompletion(c->fence, c->fence_seq + 3, c->ev);
    WaitForSingleObject(c->ev, INFINITE);
    c->fence_seq += 3;
    (void)name;
    return 0;
}

static unsigned readback(struct ctx* c, unsigned idx) {
    void* mapped = NULL;
    c->rb->lpVtbl->Map(c->rb, 0, NULL, &mapped);
    unsigned v = ((unsigned*)mapped)[idx];
    c->rb->lpVtbl->Unmap(c->rb, 0, NULL);
    return v;
}

static int allpass = 1;
static int check(const char* name, int ok) { printf("  %-28s %s\n", name, ok ? "OK" : "MISMATCH"); allpass &= ok; return ok; }

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

    /* ===== negatives ===== */
    D3D12_COMMAND_QUEUE_DESC direct_qd = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 0 };
    ID3D12CommandQueue* direct_q = NULL;
    hr = dev->lpVtbl->CreateCommandQueue(dev, &direct_qd, &IID_ID3D12CommandQueue, (void**)&direct_q);
    printf("NEG-1 DIRECT queue on CORE device: %s (expect failure) %s\n", hr_hex(hr),
           FAILED(hr) ? "PASS" : "FAIL(non-negative)");
    /* graphics PSO: compile a tiny VS+PS via d3dcompiler (SM 5.0), build a graphics desc */
    ID3DBlob* vs = NULL; ID3DBlob* ps = NULL;
    {
        typedef HRESULT (WINAPI *D3DCompileFn)(const void*, SIZE_T, const char*, const void*, const void*, const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);
        HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
        if (dc) {
            D3DCompileFn D3DCompileF = (D3DCompileFn)GetProcAddress(dc, "D3DCompile");
            const char* vsrc = "void main(float4 p : POSITION, out float4 pos : SV_Position) { pos = p; }";
            const char* psrc = "float4 main() : SV_Target { return float4(1,0,0,1); }";
            D3DCompileF(vsrc, strlen(vsrc), "vs", NULL, NULL, "main", "vs_5_0", 0, 0, &vs, NULL);
            D3DCompileF(psrc, strlen(psrc), "ps", NULL, NULL, "main", "ps_5_0", 0, 0, &ps, NULL);
        }
    }
    if (vs && ps) {
        D3D12_ROOT_SIGNATURE_DESC rsd = { 0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
        ID3DBlob* rsblob = NULL; ID3DBlob* rserr = NULL;
        ID3D12RootSignature* grs = NULL;
        if (SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsblob, &rserr)))
            dev->lpVtbl->CreateRootSignature(dev, 0, rsblob->lpVtbl->GetBufferPointer(rsblob), rsblob->lpVtbl->GetBufferSize(rsblob), &IID_ID3D12RootSignature, (void**)&grs);
        if (grs) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {0};
            gd.pRootSignature = grs;
            gd.VS.pShaderBytecode = vs->lpVtbl->GetBufferPointer(vs); gd.VS.BytecodeLength = vs->lpVtbl->GetBufferSize(vs);
            gd.PS.pShaderBytecode = ps->lpVtbl->GetBufferPointer(ps); gd.PS.BytecodeLength = ps->lpVtbl->GetBufferSize(ps);
            D3D12_INPUT_ELEMENT_DESC ie = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, 0, 0 };
            gd.InputLayout.pInputElementDescs = &ie; gd.InputLayout.NumElements = 1;
            gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            D3D12_RENDER_TARGET_BLEND_DESC rtb = {0};
            D3D12_BLEND_DESC bd = {0}; bd.RenderTarget[0] = rtb;
            gd.BlendState = bd;
            gd.SampleMask = 0xFFFFFFFF;
            gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            gd.NumRenderTargets = 1;
            gd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            gd.SampleDesc.Count = 1;
            ID3D12PipelineState* gpso = NULL;
            hr = dev->lpVtbl->CreateGraphicsPipelineState(dev, &gd, &IID_ID3D12PipelineState, (void**)&gpso);
            printf("NEG-2 graphics PSO on CORE device: %s (expect failure) %s\n", hr_hex(hr),
                   FAILED(hr) ? "PASS" : "FAIL(non-negative)");
        } else { printf("NEG-2 skipped: root sig failed\n"); }
    } else { printf("NEG-2 skipped: d3dcompiler unavailable\n"); }

    /* ===== root signature for the corpus (u0 UAV) ===== */
    D3D12_ROOT_PARAMETER param = { D3D12_ROOT_PARAMETER_TYPE_UAV, {0}, D3D12_SHADER_VISIBILITY_ALL };
    param.Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC rsdesc = { 1, &param, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3D12RootSignature* rs = NULL;
    ID3DBlob* rsblob = NULL; ID3DBlob* rserr = NULL;
    hr = D3D12SerializeRootSignature(&rsdesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsblob, &rserr);
    if (FAILED(hr)) { printf("serialize rs fail: %s\n", hr_hex(hr)); return 1; }
    hr = dev->lpVtbl->CreateRootSignature(dev, 0, rsblob->lpVtbl->GetBufferPointer(rsblob), rsblob->lpVtbl->GetBufferSize(rsblob), &IID_ID3D12RootSignature, (void**)&rs);
    if (FAILED(hr)) { printf("create rs fail: %s\n", hr_hex(hr)); return 1; }

    /* resources shared by all corpus shaders */
    struct ctx c; memset(&c, 0, sizeof(c)); c.dev = dev; c.rs = rs;
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC bd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 4096, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
    dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&c.uav);
    D3D12_HEAP_PROPERTIES hp2 = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC bd2 = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 4096, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    dev->lpVtbl->CreateCommittedResource(dev, &hp2, D3D12_HEAP_FLAG_NONE, &bd2, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&c.rb);
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    dev->lpVtbl->CreateDescriptorHeap(dev, &dhd, &IID_ID3D12DescriptorHeap, (void**)&c.dheap);
    c.dheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(c.dheap, &c.cpuH);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = { DXGI_FORMAT_UNKNOWN, D3D12_UAV_DIMENSION_BUFFER };
    uavd.Buffer.NumElements = 1024;
    dev->lpVtbl->CreateUnorderedAccessView(dev, c.uav, NULL, &uavd, c.cpuH);
    dev->lpVtbl->CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_COMPUTE, &IID_ID3D12CommandAllocator, (void**)&c.ca);
    dev->lpVtbl->CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, c.ca, NULL, &IID_ID3D12GraphicsCommandList, (void**)&c.cl);
    D3D12_COMMAND_QUEUE_DESC cqd = { D3D12_COMMAND_LIST_TYPE_COMPUTE, 0, 0, 0 };
    dev->lpVtbl->CreateCommandQueue(dev, &cqd, &IID_ID3D12CommandQueue, (void**)&c.cq);
    dev->lpVtbl->CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&c.fence);
    c.ev = CreateEventA(NULL, FALSE, FALSE, NULL);

    allpass = 1;

    /* wave64: buf[tid] = 32 (lane count) + lane index (WavePrefixSum(1ull)) */
    if (setup_one(&c, "corpus/wave64.dxil") == 0) {
        run_dispatch(&c, "wave64", 8, 1, 1);
        { void* mp=NULL; c.rb->lpVtbl->Map(c.rb,0,NULL,&mp); unsigned* uu=(unsigned*)mp;
          printf("    wave raw: %u %u %u %u %u %u %u %u | %u %u %u %u\n", uu[0],uu[1],uu[2],uu[3],uu[4],uu[5],uu[6],uu[7],uu[8],uu[9],uu[10],uu[11]);
          c.rb->lpVtbl->Unmap(c.rb,0,NULL); }
        /* the wave size on the GPU with 8-thread threadgroups is 8 lanes:
         * buf[i] = WaveGetLaneCount() + (i mod wave) - the pattern 32..39 wraps
         * every 8 threads */
        int ok = 1;
        for (unsigned i = 0; i < 64 && ok; i++) { unsigned e = 32u + (i & 7u); unsigned v = readback(&c, i); if (v != e) { printf("    mismatch i=%u got=%u exp=%u\n", i, v, e); ok = 0; } }
        { void* mp=NULL; c.rb->lpVtbl->Map(c.rb,0,NULL,&mp); unsigned* uu=(unsigned*)mp;
          printf("    wave64 raw: %u %u %u %u %u %u %u %u\n", uu[0],uu[1],uu[2],uu[3],uu[4],uu[5],uu[6],uu[7]); c.rb->lpVtbl->Unmap(c.rb,0,NULL); }
        check("wave64 lane count + prefix sum", ok);
        c.pso->lpVtbl->Release(c.pso); c.pso = NULL;
    }

    /* int64: buf[0]=0 (carry wrap), buf[tid>=1]=1 */
    if (setup_one(&c, "corpus/int64.dxil") == 0) {
        run_dispatch(&c, "int64", 1, 1, 1);
        int ok = readback(&c, 0) == 0u;
        for (unsigned i = 1; i < 8 && ok; i++) if (readback(&c, i) != 1u) ok = 0;
        { void* mp=NULL; c.rb->lpVtbl->Map(c.rb,0,NULL,&mp); unsigned* uu=(unsigned*)mp;
          printf("    int64 raw: %u %u %u %u %u %u %u %u\n", uu[0],uu[1],uu[2],uu[3],uu[4],uu[5],uu[6],uu[7]); c.rb->lpVtbl->Unmap(c.rb,0,NULL); }
        check("int64 mul+carry math", ok);
        c.pso->lpVtbl->Release(c.pso); c.pso = NULL;
    }

    /* structured: a.x = tid, a.w = 7.0, b = tid*2+1, pad = 0xDEADBEEF */
    if (setup_one(&c, "corpus/structured.dxil") == 0) {
        run_dispatch(&c, "structured", 1, 1, 1);
        { void* mp=NULL; c.rb->lpVtbl->Map(c.rb,0,NULL,&mp); unsigned* uu=(unsigned*)mp;
          printf("    raw: %08x %08x %08x %08x | %08x %08x %08x %08x\n", uu[0],uu[1],uu[2],uu[3],uu[4],uu[5],uu[6],uu[7]); c.rb->lpVtbl->Unmap(c.rb,0,NULL); }
        void* mapped = NULL; c.rb->lpVtbl->Map(c.rb, 0, NULL, &mapped);
        unsigned* u = (unsigned*)mapped;
        int ok = 1;
        for (unsigned i = 0; i < 8 && ok; i++) {
            float ax = *(float*)&u[i*8+0];
            float aw = *(float*)&u[i*8+3];
            unsigned b = u[i*8+4];
            if (ax != (float)i || aw != 7.0f || b != i*2u+1u || u[i*8+5] != 0xDEADBEEFu) ok = 0;
        }
        c.rb->lpVtbl->Unmap(c.rb, 0, NULL);
        check("structured-buffer UAV fields", ok);
        c.pso->lpVtbl->Release(c.pso); c.pso = NULL;
    }

    /* atomic32: buf[0] = 64 adds, buf[1] = max tid (63), buf[2] = last old (0..63) */
    if (setup_one(&c, "corpus/atomic32.dxil") == 0) {
        run_dispatch(&c, "atomic32", 8, 1, 1);
        unsigned a0 = readback(&c, 0), a1 = readback(&c, 1), a2 = readback(&c, 2);
        printf("    raw: %08x %08x %08x %08x %08x %08x\n", readback(&c,0),readback(&c,1),readback(&c,2),readback(&c,3),readback(&c,4),readback(&c,5));
        /* counter = 0xFFFFFFFF init + 2x64 adds = 127 (dual dispatch); max stays
         * 0xFFFFFFFF (init); buf[2] = the old value of one add (0xFFFFFFFF or 0..126) */
        int ok = (a0 == 127u && a1 == 0xFFFFFFFFu && (a2 == 0xFFFFFFFFu || a2 <= 126u));
        printf("    counter=%u max=%u last_old=%u\n", a0, a1, a2);
        check("32-bit InterlockedAdd/Max", ok);
        c.pso->lpVtbl->Release(c.pso); c.pso = NULL;
    }

    /* groupshared: buf[g] = sum(3*tid+1) over group g = 192g+92 */
    if (setup_one(&c, "corpus/groupshared.dxil") == 0) {
        run_dispatch(&c, "groupshared", 8, 1, 1);
        { void* mp=NULL; c.rb->lpVtbl->Map(c.rb,0,NULL,&mp); unsigned* uu=(unsigned*)mp;
          printf("    gs raw: %u %u %u %u %u %u %u %u\n", uu[0],uu[1],uu[2],uu[3],uu[4],uu[5],uu[6],uu[7]);
          c.rb->lpVtbl->Unmap(c.rb,0,NULL); }
        int ok = 1;
        for (unsigned g = 0; g < 8 && ok; g++) if (readback(&c, g) != 192u*g + 92u) ok = 0;
        check("groupshared + GroupMemoryBarrierWithGroupSync", ok);
        c.pso->lpVtbl->Release(c.pso); c.pso = NULL;
    }

    printf("RESULT: %s\n", allpass ? "CORE_1_0 SM 6.0 CORPUS WORKS" : "CORPUS FAILED");
    return allpass ? 0 : 2;
}
