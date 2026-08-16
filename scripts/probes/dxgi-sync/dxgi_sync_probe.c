/* DXGI-5 synchronization, pacing, and recovery probe.
 *
 * This is deliberately a synthetic queue/presentation stress test.  It proves
 * fence ordering, bounded waits, frame-latency behavior, deterministic GPU
 * readback, and resource lifetime under the pinned Wine/DXVK/vkd3d lane.  It
 * does not claim that a synthetic run is equivalent to broad game support.
 */
#include <windows.h>
#define COBJMACROS
#include <initguid.h>
#include <dxgi1_5.h>
#include <d3d12.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "../d3d12_pso_desc_ms.h"

#define SYNC_WIDTH 640
#define SYNC_HEIGHT 480
#define SYNC_BUFFERS 2
#define SYNC_MAX_FRAMES 4
#define SYNC_ROW_PITCH (((SYNC_WIDTH * 4) + 255u) & ~255u)
#define SYNC_READBACK_SIZE ((UINT64)SYNC_ROW_PITCH * SYNC_HEIGHT)
#define SYNC_WAIT_MS 5000

typedef struct SyncFrame {
    ID3D12CommandAllocator *allocator;
    ID3D12Resource *readback;
    UINT64 fence_value;
    UINT64 frame_number;
    int pending;
} SyncFrame;

typedef struct Runtime {
    IDXGIFactory1 *factory;
    IDXGIFactory2 *factory2;
    IDXGIAdapter1 *adapter;
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandQueue *copy_queue;
    ID3D12Fence *fence;
    ID3D12Fence *copy_fence;
    ID3D12GraphicsCommandList *list;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pso;
    IDXGISwapChain3 *swapchain;
    IDXGISwapChain2 *swapchain2;
    ID3D12Resource *buffers[SYNC_BUFFERS];
    ID3D12DescriptorHeap *rtv_heap;
    SyncFrame frames[SYNC_MAX_FRAMES];
    unsigned char *vs;
    unsigned char *ps;
    unsigned long vs_size;
    unsigned long ps_size;
    HANDLE fence_event;
    HANDLE copy_event;
    HANDLE frame_latency_event;
    HWND window;
    UINT rtv_stride;
    UINT in_flight;
    UINT64 next_fence;
    UINT64 next_copy_fence;
    UINT64 memory_peak;
    int tearing;
    int waitable;
    int swapchain_flags;
} Runtime;

static const char *hr_text(HRESULT hr)
{
    static char text[32];
    snprintf(text, sizeof(text), "0x%08lx", (unsigned long)hr);
    return text;
}

static int supported_hr(HRESULT hr)
{
    return hr == DXGI_ERROR_UNSUPPORTED || hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ||
            hr == E_NOTIMPL || hr == E_INVALIDARG || hr == DXGI_ERROR_WAS_STILL_DRAWING;
}

static double now_ms(void)
{
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

static void sample_memory(Runtime *runtime, const char *label, UINT64 frame)
{
    PROCESS_MEMORY_COUNTERS counters;
    memset(&counters, 0, sizeof(counters));
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        UINT64 current = (UINT64)counters.WorkingSetSize;
        if (current > runtime->memory_peak) runtime->memory_peak = current;
        printf("memory sample %-12s frame=%llu working_set=%llu peak=%llu\n", label,
                (unsigned long long)frame, (unsigned long long)current,
                (unsigned long long)runtime->memory_peak);
    } else {
        printf("memory sample %-12s frame=%llu unavailable\n", label,
                (unsigned long long)frame);
    }
}

static int load_file(const char *path, unsigned char **data, unsigned long *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (!file) return 0;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0) { fclose(file); return 0; }
    *data = (unsigned char *)malloc((size_t)length);
    if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
        free(*data); *data = NULL; fclose(file); return 0;
    }
    fclose(file);
    *size = (unsigned long)length;
    return 1;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)wparam; (void)lparam;
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static HWND create_window(void)
{
    WNDCLASSA klass;
    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = window_proc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "VKD3DProtonDXGISyncProbe";
    klass.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&klass);
    HWND window = CreateWindowExA(0, klass.lpszClassName, "VKD3D-Proton DXGI-5",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            SYNC_WIDTH, SYNC_HEIGHT, NULL, NULL, klass.hInstance, NULL);
    if (window) {
        ShowWindow(window, SW_SHOWNORMAL);
        UpdateWindow(window);
    }
    return window;
}

static void pump_messages(void)
{
    MSG message;
    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static int wait_fence(ID3D12Fence *fence, HANDLE event, UINT64 value, const char *label)
{
    DWORD result;
    double start;
    UINT64 completed = fence->lpVtbl->GetCompletedValue(fence);
    if (completed == UINT64_MAX) {
        printf("  fence wait %-18s: device-removed completed=UINT64_MAX FAIL\n", label);
        return 0;
    }
    if (completed >= value) {
        printf("  fence wait %-18s: value=%llu completed=%llu immediate PASS\n", label,
                (unsigned long long)value, (unsigned long long)completed);
        return 1;
    }
    if (FAILED(fence->lpVtbl->SetEventOnCompletion(fence, value, event))) {
        printf("  fence wait %-18s: SetEventOnCompletion FAIL\n", label);
        return 0;
    }
    start = now_ms();
    result = WaitForSingleObject(event, SYNC_WAIT_MS);
    printf("  fence wait %-18s: value=%llu result=%lu elapsed_ms=%.3f %s\n", label,
            (unsigned long long)value, (unsigned long)result, now_ms() - start,
            result == WAIT_OBJECT_0 ? "PASS" : (result == WAIT_TIMEOUT ? "TIMEOUT" : "FAIL"));
    return result == WAIT_OBJECT_0;
}

static int wait_frame(Runtime *runtime, UINT index, int verify)
{
    SyncFrame *frame = &runtime->frames[index];
    if (!frame->pending) return 1;
    if (!wait_fence(runtime->fence, runtime->fence_event, frame->fence_value, "frame"))
        return 0;
    frame->pending = 0;
    if (verify) {
        D3D12_RANGE range = { 0, SYNC_READBACK_SIZE };
        unsigned char *pixels = NULL;
        unsigned char *corner;
        unsigned char *red;
        unsigned char *green;
        unsigned char *blue;
        int ok;
        if (FAILED(frame->readback->lpVtbl->Map(frame->readback, 0, &range,
                (void **)&pixels)) || !pixels) return 0;
        corner = pixels;
        red = pixels + 360 * SYNC_ROW_PITCH + 160 * 4;
        green = pixels + 360 * SYNC_ROW_PITCH + 480 * 4;
        blue = pixels + 120 * SYNC_ROW_PITCH + 320 * 4;
        ok = red[2] > 180 && red[1] < 80 && red[0] < 80 &&
                green[1] > 180 && green[2] < 80 && green[0] < 80 &&
                blue[0] > 180 && blue[1] < 80 && blue[2] < 80 &&
                corner[2] < 80 && corner[1] < 80 && corner[0] > 20 &&
                red[3] > 200 && green[3] > 200 && blue[3] > 200;
        printf("  readback frame=%llu red=%02x%02x%02x%02x green=%02x%02x%02x%02x blue=%02x%02x%02x%02x clear=%02x%02x%02x%02x: %s\n",
                (unsigned long long)frame->frame_number,
                red[0], red[1], red[2], red[3], green[0], green[1], green[2], green[3],
                blue[0], blue[1], blue[2], blue[3], corner[0], corner[1], corner[2], corner[3],
                ok ? "PASS" : "FAIL");
        frame->readback->lpVtbl->Unmap(frame->readback, 0, NULL);
        return ok;
    }
    return 1;
}

static int create_pso(Runtime *runtime, ID3D12PipelineState **out)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC_MS desc;
    ID3DBlob *root_blob = NULL, *root_error = NULL;
    D3D12_ROOT_SIGNATURE_DESC root_desc;
    HRESULT hr;
    if (!runtime->root_signature) {
        memset(&root_desc, 0, sizeof(root_desc));
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                &root_blob, &root_error);
        if (FAILED(hr)) {
            if (root_error) root_error->lpVtbl->Release(root_error);
            return 0;
        }
        hr = runtime->device->lpVtbl->CreateRootSignature(runtime->device, 0,
                root_blob->lpVtbl->GetBufferPointer(root_blob),
                root_blob->lpVtbl->GetBufferSize(root_blob), &IID_ID3D12RootSignature,
                (void **)&runtime->root_signature);
        root_blob->lpVtbl->Release(root_blob);
        if (root_error) root_error->lpVtbl->Release(root_error);
        if (FAILED(hr)) return 0;
    }
    memset(&desc, 0, sizeof(desc));
    desc.pRootSignature = runtime->root_signature;
    desc.VS.pShaderBytecode = runtime->vs;
    desc.VS.BytecodeLength = runtime->vs_size;
    desc.PS.pShaderBytecode = runtime->ps;
    desc.PS.BytecodeLength = runtime->ps_size;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    desc.SampleMask = 0xffffffffu;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    hr = runtime->device->lpVtbl->CreateGraphicsPipelineState(runtime->device,
            (const D3D12_GRAPHICS_PIPELINE_STATE_DESC *)(const void *)&desc,
            &IID_ID3D12PipelineState, (void **)out);
    return SUCCEEDED(hr);
}

static int create_readback(Runtime *runtime, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;
    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = SYNC_READBACK_SIZE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(runtime->device->lpVtbl->CreateCommittedResource(runtime->device,
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)out));
}

static int create_runtime(Runtime *runtime)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    DXGI_ADAPTER_DESC1 adapter_desc;
    LUID device_luid;
    HRESULT hr;
    UINT i;
    char *in_flight_env;
    memset(runtime, 0, sizeof(*runtime));
    runtime->in_flight = 3;
    in_flight_env = getenv("DXGI_SYNC_IN_FLIGHT");
    if (in_flight_env && *in_flight_env) {
        UINT requested = (UINT)strtoul(in_flight_env, NULL, 10);
        if (requested >= 2 && requested <= 4) runtime->in_flight = requested;
    }
    runtime->window = create_window();
    if (!runtime->window) { printf("window: FAIL\n"); return 0; }
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&runtime->factory);
    if (FAILED(hr)) { printf("factory: %s\n", hr_text(hr)); return 0; }
    hr = runtime->factory->lpVtbl->QueryInterface(runtime->factory, &IID_IDXGIFactory2,
            (void **)&runtime->factory2);
    if (FAILED(hr)) { printf("factory2: %s\n", hr_text(hr)); return 0; }
    runtime->factory->lpVtbl->MakeWindowAssociation(runtime->factory, runtime->window,
            DXGI_MWA_NO_ALT_ENTER);
    for (i = 0; runtime->factory->lpVtbl->EnumAdapters1(runtime->factory, i,
            &runtime->adapter) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 desc;
        runtime->adapter->lpVtbl->GetDesc1(runtime->adapter, &desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
        IDXGIAdapter1_Release(runtime->adapter);
        runtime->adapter = NULL;
    }
    if (!runtime->adapter) { printf("hardware adapter: FAIL\n"); return 0; }
    memset(&adapter_desc, 0, sizeof(adapter_desc));
    hr = runtime->adapter->lpVtbl->GetDesc1(runtime->adapter, &adapter_desc);
    if (FAILED(hr)) return 0;
    printf("selected adapter: vendor=0x%04x device=0x%04x luid=%08lx-%08lx\n",
            adapter_desc.VendorId, adapter_desc.DeviceId,
            (unsigned long)adapter_desc.AdapterLuid.HighPart,
            (unsigned long)adapter_desc.AdapterLuid.LowPart);
    hr = D3D12CreateDevice((IUnknown *)runtime->adapter, D3D_FEATURE_LEVEL_11_0,
            &IID_ID3D12Device, (void **)&runtime->device);
    if (FAILED(hr)) { printf("D3D12CreateDevice: %s\n", hr_text(hr)); return 0; }
    memset(&device_luid, 0, sizeof(device_luid));
    runtime->device->lpVtbl->GetAdapterLuid(runtime->device, &device_luid);
    printf("D3D12 adapter LUID: %08lx-%08lx (%s)\n",
            (unsigned long)device_luid.HighPart, (unsigned long)device_luid.LowPart,
            memcmp(&adapter_desc.AdapterLuid, &device_luid, sizeof(LUID)) == 0 ? "MATCH" : "MISMATCH");
    if (memcmp(&adapter_desc.AdapterLuid, &device_luid, sizeof(LUID)) != 0) return 0;
    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = runtime->device->lpVtbl->CreateCommandQueue(runtime->device, &queue_desc,
            &IID_ID3D12CommandQueue, (void **)&runtime->queue);
    if (FAILED(hr)) return 0;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    hr = runtime->device->lpVtbl->CreateCommandQueue(runtime->device, &queue_desc,
            &IID_ID3D12CommandQueue, (void **)&runtime->copy_queue);
    if (FAILED(hr)) {
        runtime->copy_queue = NULL;
        printf("copy queue: hr=%s UNSUPPORTED\n", hr_text(hr));
    } else printf("copy queue: PASS\n");
    hr = runtime->device->lpVtbl->CreateFence(runtime->device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&runtime->fence);
    if (FAILED(hr)) return 0;
    if (runtime->copy_queue && FAILED(runtime->device->lpVtbl->CreateFence(runtime->device, 0,
            D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&runtime->copy_fence))) {
        runtime->copy_fence = NULL;
    }
    runtime->fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    runtime->copy_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!runtime->fence_event || !runtime->copy_event) return 0;
    if (!load_file("dxgi-present/triangle_vs.dxil", &runtime->vs, &runtime->vs_size) ||
            !load_file("dxgi-present/triangle_ps.dxil", &runtime->ps, &runtime->ps_size)) {
        printf("shader files: MISSING\n"); return 0;
    }
    if (!create_pso(runtime, &runtime->pso)) { printf("graphics PSO: FAIL\n"); return 0; }
    runtime->swapchain_flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    {
        IDXGIFactory5 *factory5 = NULL;
        BOOL tearing = FALSE;
        hr = runtime->factory->lpVtbl->QueryInterface(runtime->factory, &IID_IDXGIFactory5,
                (void **)&factory5);
        if (SUCCEEDED(hr)) {
            hr = factory5->lpVtbl->CheckFeatureSupport(factory5,
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
            runtime->tearing = SUCCEEDED(hr) && tearing;
            printf("tearing support: hr=%s supported=%u\n", hr_text(hr), tearing);
            IDXGIFactory5_Release(factory5);
        } else printf("tearing support: unavailable (%s)\n", hr_text(hr));
    }
    if (runtime->tearing) runtime->swapchain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    {
        DXGI_SWAP_CHAIN_DESC1 desc;
        IDXGISwapChain1 *swapchain1 = NULL;
        memset(&desc, 0, sizeof(desc));
        desc.Width = SYNC_WIDTH; desc.Height = SYNC_HEIGHT;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = SYNC_BUFFERS; desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = runtime->swapchain_flags;
        hr = runtime->factory2->lpVtbl->CreateSwapChainForHwnd(runtime->factory2,
                (IUnknown *)runtime->queue, runtime->window, &desc, NULL, NULL, &swapchain1);
        if (FAILED(hr) && (desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)) {
            printf("frame-latency swapchain: hr=%s UNSUPPORTED; retrying without waitable flag\n", hr_text(hr));
            desc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            runtime->swapchain_flags = desc.Flags;
            hr = runtime->factory2->lpVtbl->CreateSwapChainForHwnd(runtime->factory2,
                    (IUnknown *)runtime->queue, runtime->window, &desc, NULL, NULL, &swapchain1);
        }
        if (FAILED(hr)) { printf("CreateSwapChainForHwnd: %s\n", hr_text(hr)); return 0; }
        hr = swapchain1->lpVtbl->QueryInterface(swapchain1, &IID_IDXGISwapChain3,
                (void **)&runtime->swapchain);
        if (FAILED(hr)) { IDXGISwapChain1_Release(swapchain1); return 0; }
        hr = swapchain1->lpVtbl->QueryInterface(swapchain1, &IID_IDXGISwapChain2,
                (void **)&runtime->swapchain2);
        IDXGISwapChain1_Release(swapchain1);
        if (SUCCEEDED(hr) && (runtime->swapchain_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)) {
            hr = runtime->swapchain2->lpVtbl->SetMaximumFrameLatency(runtime->swapchain2, runtime->in_flight);
            if (SUCCEEDED(hr)) {
                runtime->frame_latency_event = runtime->swapchain2->lpVtbl->GetFrameLatencyWaitableObject(runtime->swapchain2);
                runtime->waitable = runtime->frame_latency_event != NULL;
            }
            printf("frame latency object: SetMaximumFrameLatency(%u) hr=%s %s\n",
                    runtime->in_flight, hr_text(hr), runtime->waitable ? "SUPPORTED" : "UNSUPPORTED");
        } else printf("frame latency object: NOT EXPOSED (%s)\n", hr_text(hr));
    }
    runtime->rtv_stride = runtime->device->lpVtbl->GetDescriptorHandleIncrementSize(runtime->device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = SYNC_BUFFERS;
    hr = runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&runtime->rtv_heap);
    if (FAILED(hr)) return 0;
    runtime->rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(runtime->rtv_heap, &start);
    for (i = 0; i < SYNC_BUFFERS; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = start;
        handle.ptr += (SIZE_T)i * runtime->rtv_stride;
        hr = runtime->swapchain->lpVtbl->GetBuffer(runtime->swapchain, i,
                &IID_ID3D12Resource, (void **)&runtime->buffers[i]);
        if (FAILED(hr)) return 0;
        runtime->device->lpVtbl->CreateRenderTargetView(runtime->device,
                runtime->buffers[i], NULL, handle);
    }
    for (i = 0; i < runtime->in_flight; i++) {
        hr = runtime->device->lpVtbl->CreateCommandAllocator(runtime->device,
                D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                (void **)&runtime->frames[i].allocator);
        if (FAILED(hr) || !create_readback(runtime, &runtime->frames[i].readback)) return 0;
    }
    hr = runtime->device->lpVtbl->CreateCommandList(runtime->device, 0,
            D3D12_COMMAND_LIST_TYPE_DIRECT, runtime->frames[0].allocator, runtime->pso,
            &IID_ID3D12GraphicsCommandList, (void **)&runtime->list);
    if (FAILED(hr)) return 0;
    runtime->list->lpVtbl->Close(runtime->list);
    printf("window/device/queue/swapchain: PASS\n");
    return 1;
}

static int submit_frame(Runtime *runtime, UINT slot, UINT64 frame_number, UINT sync_interval)
{
    SyncFrame *frame = &runtime->frames[slot];
    UINT index = runtime->swapchain->lpVtbl->GetCurrentBackBufferIndex(runtime->swapchain);
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    D3D12_RESOURCE_BARRIER barrier;
    D3D12_VIEWPORT viewport = { 0, 0, SYNC_WIDTH, SYNC_HEIGHT, 0, 1 };
    D3D12_RECT scissor = { 0, 0, SYNC_WIDTH, SYNC_HEIGHT };
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 row_size, total_size;
    UINT rows;
    ID3D12CommandList *lists[1];
    HRESULT hr;
    float clear_color[4] = { 0.02f, 0.02f, 0.20f, 1.0f };
    if (index >= SYNC_BUFFERS || !wait_frame(runtime, slot, 1)) return 0;
    if (runtime->waitable && frame_number >= runtime->in_flight) {
        DWORD wait_result = WaitForSingleObject(runtime->frame_latency_event, SYNC_WAIT_MS);
        printf("  frame-latency wait frame=%llu result=%lu %s\n",
                (unsigned long long)frame_number, (unsigned long)wait_result,
                wait_result == WAIT_OBJECT_0 ? "PASS" : "TIMEOUT");
        if (wait_result != WAIT_OBJECT_0) return 0;
    }
    if (FAILED(frame->allocator->lpVtbl->Reset(frame->allocator)) ||
            FAILED(runtime->list->lpVtbl->Reset(runtime->list, frame->allocator, runtime->pso))) return 0;
    runtime->rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(runtime->rtv_heap, &handle);
    handle.ptr += (SIZE_T)index * runtime->rtv_stride;
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = runtime->buffers[index];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    runtime->list->lpVtbl->OMSetRenderTargets(runtime->list, 1, &handle, FALSE, NULL);
    runtime->list->lpVtbl->ClearRenderTargetView(runtime->list, handle, clear_color, 0, NULL);
    runtime->list->lpVtbl->RSSetViewports(runtime->list, 1, &viewport);
    runtime->list->lpVtbl->RSSetScissorRects(runtime->list, 1, &scissor);
    runtime->list->lpVtbl->IASetPrimitiveTopology(runtime->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    runtime->list->lpVtbl->DrawInstanced(runtime->list, 3, 1, 0, 0);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    memset(&footprint, 0, sizeof(footprint));
    runtime->device->lpVtbl->GetCopyableFootprints(runtime->device,
            &(D3D12_RESOURCE_DESC){ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)SYNC_WIDTH,
                SYNC_HEIGHT, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 },
                D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE }, 0, 1, 0,
            &footprint, &rows, &row_size, &total_size);
    memset(&dst, 0, sizeof(dst));
    dst.pResource = frame->readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    memset(&src, 0, sizeof(src));
    src.pResource = runtime->buffers[index];
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    runtime->list->lpVtbl->CopyTextureRegion(runtime->list, &dst, 0, 0, 0, &src, NULL);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    if (FAILED(runtime->list->lpVtbl->Close(runtime->list))) return 0;
    lists[0] = (ID3D12CommandList *)runtime->list;
    runtime->queue->lpVtbl->ExecuteCommandLists(runtime->queue, 1, lists);
    frame->fence_value = ++runtime->next_fence;
    hr = runtime->queue->lpVtbl->Signal(runtime->queue, runtime->fence, frame->fence_value);
    if (FAILED(hr)) return 0;
    frame->frame_number = frame_number;
    frame->pending = 1;
    hr = runtime->swapchain->lpVtbl->Present(runtime->swapchain, sync_interval,
            sync_interval == 0 && runtime->tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (hr == DXGI_STATUS_OCCLUDED) {
        printf("  Present frame=%llu: hr=%s OCCLUDED\n", (unsigned long long)frame_number, hr_text(hr));
        return 1;
    }
    if (FAILED(hr)) {
        printf("  Present frame=%llu: hr=%s FAIL\n", (unsigned long long)frame_number, hr_text(hr));
        return 0;
    }
    return 1;
}

static int resource_churn(Runtime *runtime, UINT64 frame_number)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *resources[8] = { 0 };
    ID3D12PipelineState *pso = NULL;
    UINT i;
    int ok = 1;
    HRESULT hr;
    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 4096 + (frame_number & 4095);
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (i = 0; i < 8; i++) {
        hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void **)&resources[i]);
        if (FAILED(hr)) { ok = 0; break; }
        void *mapped = NULL;
        if (SUCCEEDED(resources[i]->lpVtbl->Map(resources[i], 0, NULL, &mapped)) && mapped) {
            memset(mapped, (int)(frame_number + i), 4096);
            resources[i]->lpVtbl->Unmap(resources[i], 0, NULL);
        }
    }
    if (ok && !create_pso(runtime, &pso)) ok = 0;
    if (pso) ID3D12PipelineState_Release(pso);
    while (i > 0) {
        --i;
        ID3D12Resource_Release(resources[i]);
    }
    printf("  resource/pipeline churn frame=%llu: %s\n", (unsigned long long)frame_number,
            ok ? "PASS" : "FAIL");
    return ok;
}

static int check_device_reason(Runtime *runtime, const char *phase)
{
    HRESULT hr = runtime->device->lpVtbl->GetDeviceRemovedReason(runtime->device);
    printf("GetDeviceRemovedReason(%s): hr=%s %s\n", phase, hr_text(hr),
            hr == S_OK ? "S_OK" : "REPORTED");
    return hr == S_OK;
}

static int queue_and_fence_tests(Runtime *runtime)
{
    HRESULT hr;
    UINT64 value;
    int failures = 0;
    ID3D12Fence *timeout_fence = NULL;
    HANDLE timeout_event = NULL;
    DWORD timeout_result;
    value = ++runtime->next_fence;
    hr = runtime->queue->lpVtbl->Signal(runtime->queue, runtime->fence, value);
    printf("  queue Signal: hr=%s %s\n", hr_text(hr), SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (FAILED(hr) || !wait_fence(runtime->fence, runtime->fence_event, value, "signal/event")) failures++;
    if (runtime->copy_queue && runtime->copy_fence) {
        value = ++runtime->next_copy_fence;
        hr = runtime->copy_queue->lpVtbl->Signal(runtime->copy_queue, runtime->copy_fence, value);
        if (SUCCEEDED(hr)) hr = runtime->queue->lpVtbl->Wait(runtime->queue, runtime->copy_fence, value);
        printf("  cross-queue Signal/Wait: hr=%s %s\n", hr_text(hr), SUCCEEDED(hr) ? "PASS" : "UNSUPPORTED");
        if (FAILED(hr) && !supported_hr(hr)) failures++;
        if (SUCCEEDED(hr) && !wait_fence(runtime->copy_fence, runtime->copy_event, value, "copy queue")) failures++;
    } else printf("  cross-queue Signal/Wait: UNSUPPORTED\n");
    hr = runtime->fence->lpVtbl->SetEventOnCompletion(runtime->fence, runtime->next_fence, NULL);
    printf("  invalid fence event: hr=%s %s\n", hr_text(hr), FAILED(hr) ? "PASS" : "REPORTED");
    if (SUCCEEDED(hr)) printf("    invalid event accepted by backend; no wait was issued\n");
    if (!check_device_reason(runtime, "after invalid operation")) failures++;
    hr = runtime->device->lpVtbl->CreateFence(runtime->device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&timeout_fence);
    timeout_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (SUCCEEDED(hr) && timeout_event &&
            SUCCEEDED(timeout_fence->lpVtbl->SetEventOnCompletion(timeout_fence, 1, timeout_event))) {
        timeout_result = WaitForSingleObject(timeout_event, 25);
        printf("  bounded unsignaled fence timeout: result=%lu %s\n",
                (unsigned long)timeout_result,
                timeout_result == WAIT_TIMEOUT ? "TIMEOUT PASS" : "FAIL");
        if (timeout_result != WAIT_TIMEOUT) failures++;
    } else {
        printf("  bounded unsignaled fence timeout: UNSUPPORTED\n");
    }
    if (timeout_fence) ID3D12Fence_Release(timeout_fence);
    if (timeout_event) CloseHandle(timeout_event);
    if (!check_device_reason(runtime, "after timeout path")) failures++;
    /* Do not submit a lower queue signal: it can poison the backend timeline
     * and turn a negative test into a process crash. */
    printf("  out-of-order fence signal: UNSUPPORTED (unsafe operation not issued)\n");
    printf("  fence semantics: %s\n", failures ? "FAIL" : "PASS");
    return failures == 0;
}

static int release_before_completion(Runtime *runtime)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *src = NULL, *dst = NULL;
    HRESULT hr;
    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = 4096;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&src);
    if (FAILED(hr)) return 0;
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&dst);
    if (FAILED(hr)) { ID3D12Resource_Release(src); return 0; }
    /* Do not submit an intentionally unsafe early-release operation.  This
     * backend asserts inside vkQueueSubmit2 when that negative is forced, so
     * classify the capability boundary without crashing the process. */
    ID3D12Resource_Release(src);
    ID3D12Resource_Release(dst);
    printf("  release before GPU completion: UNSUPPORTED (unsafe negative not issued)\n");
    return 1;
}

static int occlusion_and_trim(Runtime *runtime)
{
    HRESULT hr;
    IDXGIDevice3 *device3 = NULL;
    hr = runtime->swapchain->lpVtbl->Present(runtime->swapchain, 0, DXGI_PRESENT_TEST);
    printf("  DXGI_PRESENT_TEST: hr=%s %s\n", hr_text(hr), SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED ? "PASS" : "FAIL");
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) return 0;
    if (!check_device_reason(runtime, "after occlusion")) return 0;
    hr = runtime->device->lpVtbl->QueryInterface(runtime->device, &IID_IDXGIDevice3,
            (void **)&device3);
    if (SUCCEEDED(hr)) {
        device3->lpVtbl->Trim(device3);
        printf("  IDXGIDevice3::Trim: S_OK PASS\n");
        IDXGIDevice3_Release(device3);
    } else printf("  IDXGIDevice3::Trim: hr=%s UNSUPPORTED\n", hr_text(hr));
    return 1;
}

static int run_frames(Runtime *runtime, UINT64 count, UINT sync_interval, UINT64 base)
{
    UINT64 frame;
    int failures = 0;
    UINT64 sample_a = base, sample_b = base + count / 2, sample_c = base + count - 1;
    for (frame = 0; frame < count; frame++) {
        UINT64 absolute = base + frame;
        UINT slot = (UINT)(frame % runtime->in_flight);
        pump_messages();
        if (!submit_frame(runtime, slot, absolute, sync_interval)) { failures++; break; }
        if (runtime->frames[slot].pending && runtime->frames[slot].frame_number != absolute) failures++;
        if (frame >= runtime->in_flight) {
            UINT old_slot = (UINT)((frame - runtime->in_flight) % runtime->in_flight);
            int verify = runtime->frames[old_slot].frame_number == sample_a ||
                    runtime->frames[old_slot].frame_number == sample_b ||
                    runtime->frames[old_slot].frame_number == sample_c;
            if (!wait_frame(runtime, old_slot, verify)) { failures++; break; }
        }
        if ((absolute % 1000) == 0) {
            if (!resource_churn(runtime, absolute)) failures++;
        }
        if ((absolute % 10000) == 0) sample_memory(runtime, "stress", absolute);
    }
    for (UINT i = 0; i < runtime->in_flight; i++) {
        int verify = runtime->frames[i].frame_number == sample_a ||
                runtime->frames[i].frame_number == sample_b || runtime->frames[i].frame_number == sample_c;
        if (!wait_frame(runtime, i, verify)) failures++;
    }
    printf("  frame run interval=%u frames=%llu: %s\n", sync_interval,
            (unsigned long long)count, failures ? "FAIL" : "PASS");
    return failures == 0;
}

static void shutdown_runtime(Runtime *runtime)
{
    UINT i;
    printf("  shutdown: GPU idle\n");
    for (i = 0; i < runtime->in_flight; i++) wait_frame(runtime, i, 0);
    printf("  shutdown: RTVs/resources\n");
    for (i = 0; i < SYNC_BUFFERS; i++) {
        if (runtime->buffers[i]) ID3D12Resource_Release(runtime->buffers[i]);
        runtime->buffers[i] = NULL;
    }
    if (runtime->rtv_heap) ID3D12DescriptorHeap_Release(runtime->rtv_heap);
    for (i = 0; i < runtime->in_flight; i++) {
        if (runtime->frames[i].readback) ID3D12Resource_Release(runtime->frames[i].readback);
        if (runtime->frames[i].allocator) ID3D12CommandAllocator_Release(runtime->frames[i].allocator);
    }
    if (runtime->list) ID3D12GraphicsCommandList_Release(runtime->list);
    if (runtime->pso) ID3D12PipelineState_Release(runtime->pso);
    if (runtime->root_signature) ID3D12RootSignature_Release(runtime->root_signature);
    printf("  shutdown: swapchain\n");
    if (runtime->swapchain2) IDXGISwapChain2_Release(runtime->swapchain2);
    if (runtime->swapchain) IDXGISwapChain3_Release(runtime->swapchain);
    printf("  shutdown: queue/fence\n");
    if (runtime->copy_fence) ID3D12Fence_Release(runtime->copy_fence);
    if (runtime->copy_queue) ID3D12CommandQueue_Release(runtime->copy_queue);
    if (runtime->fence) ID3D12Fence_Release(runtime->fence);
    if (runtime->queue) ID3D12CommandQueue_Release(runtime->queue);
    if (runtime->fence_event) CloseHandle(runtime->fence_event);
    if (runtime->copy_event) CloseHandle(runtime->copy_event);
    printf("  shutdown: device\n");
    if (runtime->device) {
        HRESULT hr = runtime->device->lpVtbl->GetDeviceRemovedReason(runtime->device);
        printf("  GetDeviceRemovedReason(shutdown): hr=%s %s\n", hr_text(hr),
                hr == S_OK ? "S_OK" : "REPORTED");
        ID3D12Device_Release(runtime->device);
    }
    printf("  shutdown: adapter\n");
    if (runtime->adapter) IDXGIAdapter1_Release(runtime->adapter);
    printf("  shutdown: factory\n");
    if (runtime->factory2) IDXGIFactory2_Release(runtime->factory2);
    if (runtime->factory) IDXGIFactory1_Release(runtime->factory);
    printf("  shutdown: window\n");
    if (runtime->window) DestroyWindow(runtime->window);
    free(runtime->vs); free(runtime->ps);
    memset(runtime, 0, sizeof(*runtime));
}

int main(void)
{
    Runtime runtime;
    UINT64 frames = 100000;
    char *env = getenv("DXGI_SYNC_FRAMES");
    char *in_flight_env = getenv("DXGI_SYNC_IN_FLIGHT");
    UINT64 short_frames = 8;
    int failures = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (env && *env) frames = strtoull(env, NULL, 10);
    printf("=== DXGI-5 synchronization/pacing/recovery probe ===\n");
    printf("frames=%llu, accepted frames-in-flight=2..4, deterministic RGB triangle\n",
            (unsigned long long)frames);
    if (!create_runtime(&runtime)) {
        shutdown_runtime(&runtime);
        return 1;
    }
    printf("frames-in-flight accepted mode=%u\n", runtime.in_flight);
    sample_memory(&runtime, "baseline", 0);
    if (!queue_and_fence_tests(&runtime)) failures++;
    if (!release_before_completion(&runtime)) failures++;
    if (!occlusion_and_trim(&runtime)) failures++;
    printf("=== pacing smoke sync interval 0 ===\n");
    if (!run_frames(&runtime, short_frames, 0, 0)) failures++;
    printf("=== pacing smoke sync interval 1 ===\n");
    if (!run_frames(&runtime, short_frames, 1, short_frames)) failures++;
    printf("=== long synchronization stress ===\n");
    if (!run_frames(&runtime, frames, 0, short_frames * 2)) failures++;
    sample_memory(&runtime, "final", frames + short_frames * 2);
    {
        HRESULT hr = runtime.device->lpVtbl->GetDeviceRemovedReason(runtime.device);
        printf("GetDeviceRemovedReason(normal): hr=%s %s\n", hr_text(hr), hr == S_OK ? "PASS" : "REPORTED");
        if (FAILED(hr)) failures++;
    }
    printf("memory bounded: baseline/peak/final samples recorded peak=%llu: %s\n",
            (unsigned long long)runtime.memory_peak, runtime.memory_peak ? "PASS" : "UNAVAILABLE");
    shutdown_runtime(&runtime);
    printf("DXGI-5 result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
            failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
