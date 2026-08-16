/* DXGI-3 window lifecycle probe.
 * Resize, minimize/restore, occlusion, fullscreen fallback, destruction, and
 * repeated create/resize/destroy behavior are covered here. Broad gameplay
 * stability and later format/HDR work remain separate phases.
 */
#include <windows.h>
#define COBJMACROS
#include <initguid.h>
#include <dxgi1_5.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../d3d12_pso_desc_ms.h"

#define WIDTH 640
#define HEIGHT 480
#define BUFFER_COUNT 2
#define FRAME_COUNT 1000
#define ROW_PITCH (WIDTH * 4)
#define READBACK_SIZE ((UINT64)ROW_PITCH * HEIGHT)

typedef struct Runtime {
    IDXGIFactory1 *factory;
    IDXGIFactory2 *factory2;
    IDXGIAdapter1 *adapter;
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    HANDLE fence_event;
    UINT64 fence_value;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pso;
    unsigned char *vs;
    unsigned char *ps;
    unsigned long vs_size;
    unsigned long ps_size;
    HWND window;
    BOOL tearing_supported;
} Runtime;

typedef struct ModeResult {
    const char *name;
    int for_hwnd;
    DXGI_SWAP_EFFECT effect;
    int present1;
    HRESULT create_hr;
    HRESULT present_test_hr;
    HRESULT first_present_hr;
    HRESULT last_present_hr;
    HRESULT frame_stats_hr;
    HRESULT last_count_hr;
    UINT last_count;
    UINT64 frame_count;
    int pixel_ok;
    int supported;
} ModeResult;

static const char *hr_text(HRESULT hr)
{
    static char text[32];
    snprintf(text, sizeof(text), "0x%08lx", (unsigned long)hr);
    return text;
}

static int hr_ok_or_unsupported(HRESULT hr)
{
    return SUCCEEDED(hr) || hr == DXGI_ERROR_UNSUPPORTED || hr == E_NOTIMPL;
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
    *data = malloc((size_t)length);
    if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
        free(*data); *data = NULL; fclose(file); return 0;
    }
    fclose(file);
    *size = (unsigned long)length;
    return 1;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static HWND create_window(void)
{
    WNDCLASSA klass;
    HWND window;
    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = window_proc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "VKD3DProtonDXGILifecycleProbe";
    klass.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&klass);
    window = CreateWindowExA(0, klass.lpszClassName, "VKD3D-Proton DXGI-3",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT,
            NULL, NULL, klass.hInstance, NULL);
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

static int wait_fence(Runtime *runtime)
{
    if (runtime->fence->lpVtbl->GetCompletedValue(runtime->fence) < runtime->fence_value) {
        HRESULT hr = runtime->fence->lpVtbl->SetEventOnCompletion(runtime->fence,
                runtime->fence_value, runtime->fence_event);
        if (FAILED(hr)) return 0;
        if (WaitForSingleObject(runtime->fence_event, 30000) != WAIT_OBJECT_0)
            return 0;
    }
    return 1;
}

static int signal_and_wait(Runtime *runtime)
{
    runtime->fence_value++;
    if (FAILED(runtime->queue->lpVtbl->Signal(runtime->queue, runtime->fence,
            runtime->fence_value))) return 0;
    return wait_fence(runtime);
}

static void release_runtime(Runtime *runtime)
{
    if (runtime->queue && runtime->fence) {
        runtime->fence_value++;
        runtime->queue->lpVtbl->Signal(runtime->queue, runtime->fence, runtime->fence_value);
        wait_fence(runtime);
    }
    if (runtime->pso) ID3D12PipelineState_Release(runtime->pso);
    if (runtime->root_signature) ID3D12RootSignature_Release(runtime->root_signature);
    if (runtime->list) ID3D12GraphicsCommandList_Release(runtime->list);
    if (runtime->allocator) ID3D12CommandAllocator_Release(runtime->allocator);
    if (runtime->fence) ID3D12Fence_Release(runtime->fence);
    if (runtime->queue) ID3D12CommandQueue_Release(runtime->queue);
    if (runtime->device) ID3D12Device_Release(runtime->device);
    if (runtime->adapter) IDXGIAdapter1_Release(runtime->adapter);
    if (runtime->factory2) IDXGIFactory2_Release(runtime->factory2);
    if (runtime->factory) IDXGIFactory1_Release(runtime->factory);
    if (runtime->fence_event) CloseHandle(runtime->fence_event);
    if (runtime->window) DestroyWindow(runtime->window);
    free(runtime->vs);
    free(runtime->ps);
    memset(runtime, 0, sizeof(*runtime));
}

static int create_runtime(Runtime *runtime)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    D3D12_ROOT_SIGNATURE_DESC root_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC_MS pso_desc;
    ID3DBlob *root_blob = NULL, *root_error = NULL;
    DXGI_ADAPTER_DESC1 adapter_desc;
    LUID device_luid;
    HRESULT hr;
    UINT i;

    memset(runtime, 0, sizeof(*runtime));
    runtime->window = create_window();
    if (!runtime->window) { printf("window: FAIL\n"); return 0; }

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&runtime->factory);
    if (FAILED(hr)) { printf("factory: %s\n", hr_text(hr)); return 0; }
    hr = runtime->factory->lpVtbl->QueryInterface(runtime->factory,
            &IID_IDXGIFactory2, (void **)&runtime->factory2);
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
    if (FAILED(hr)) { printf("adapter identity: FAIL %s\n", hr_text(hr)); return 0; }
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
            (unsigned long)device_luid.HighPart,
            (unsigned long)device_luid.LowPart,
            memcmp(&adapter_desc.AdapterLuid, &device_luid, sizeof(LUID)) == 0 ? "MATCH" : "MISMATCH");
    if (memcmp(&adapter_desc.AdapterLuid, &device_luid, sizeof(LUID)) != 0) {
        printf("adapter identity: FAIL\n");
        return 0;
    }

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = runtime->device->lpVtbl->CreateCommandQueue(runtime->device, &queue_desc,
            &IID_ID3D12CommandQueue, (void **)&runtime->queue);
    if (FAILED(hr)) { printf("CreateCommandQueue: %s\n", hr_text(hr)); return 0; }
    hr = runtime->device->lpVtbl->CreateCommandAllocator(runtime->device,
            D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
            (void **)&runtime->allocator);
    if (FAILED(hr)) return 0;
    hr = runtime->device->lpVtbl->CreateCommandList(runtime->device, 0,
            D3D12_COMMAND_LIST_TYPE_DIRECT, runtime->allocator, NULL,
            &IID_ID3D12GraphicsCommandList, (void **)&runtime->list);
    if (FAILED(hr)) return 0;
    runtime->list->lpVtbl->Close(runtime->list);
    hr = runtime->device->lpVtbl->CreateFence(runtime->device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&runtime->fence);
    if (FAILED(hr)) return 0;
    runtime->fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!runtime->fence_event) return 0;

    if (!load_file("dxgi-present/triangle_vs.dxil", &runtime->vs, &runtime->vs_size) ||
            !load_file("dxgi-present/triangle_ps.dxil", &runtime->ps, &runtime->ps_size)) {
        printf("shader files: MISSING\n");
        return 0;
    }
    memset(&root_desc, 0, sizeof(root_desc));
    hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &root_blob, &root_error);
    if (FAILED(hr)) { printf("root signature: %s\n", hr_text(hr)); return 0; }
    hr = runtime->device->lpVtbl->CreateRootSignature(runtime->device, 0,
            root_blob->lpVtbl->GetBufferPointer(root_blob),
            root_blob->lpVtbl->GetBufferSize(root_blob), &IID_ID3D12RootSignature,
            (void **)&runtime->root_signature);
    if (root_blob) root_blob->lpVtbl->Release(root_blob);
    if (root_error) root_error->lpVtbl->Release(root_error);
    if (FAILED(hr)) return 0;

    memset(&pso_desc, 0, sizeof(pso_desc));
    pso_desc.pRootSignature = runtime->root_signature;
    pso_desc.VS.pShaderBytecode = runtime->vs;
    pso_desc.VS.BytecodeLength = runtime->vs_size;
    pso_desc.PS.pShaderBytecode = runtime->ps;
    pso_desc.PS.BytecodeLength = runtime->ps_size;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso_desc.SampleMask = 0xffffffffu;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;
    hr = runtime->device->lpVtbl->CreateGraphicsPipelineState(runtime->device,
            (const D3D12_GRAPHICS_PIPELINE_STATE_DESC *)(const void *)&pso_desc,
            &IID_ID3D12PipelineState, (void **)&runtime->pso);
    if (FAILED(hr)) { printf("graphics PSO: %s\n", hr_text(hr)); return 0; }

    {
        IDXGIFactory5 *factory5 = NULL;
        BOOL tearing = FALSE;
        hr = runtime->factory->lpVtbl->QueryInterface(runtime->factory,
                &IID_IDXGIFactory5, (void **)&factory5);
        if (SUCCEEDED(hr)) {
            hr = factory5->lpVtbl->CheckFeatureSupport(factory5,
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
            printf("tearing support: hr=%s supported=%u\n", hr_text(hr), tearing);
            if (SUCCEEDED(hr)) runtime->tearing_supported = tearing;
            IDXGIFactory5_Release(factory5);
        } else {
            printf("tearing support: unavailable (%s)\n", hr_text(hr));
        }
    }
    printf("window/device/queue: PASS\n");
    return 1;
}

static int verify_pixels(ID3D12Resource *readback)
{
    D3D12_RANGE range = { 0, READBACK_SIZE };
    unsigned char *pixels = NULL;
    unsigned char red[4];
    unsigned char green[4];
    unsigned char blue[4];
    unsigned char corner[4];
    int ok;
    if (FAILED(readback->lpVtbl->Map(readback, 0, &range, (void **)&pixels)) || !pixels)
        return 0;
    /* Sample well inside each colored vertex region, away from edges. */
    memcpy(red, pixels + 360 * ROW_PITCH + 160 * 4, 4);
    memcpy(green, pixels + 360 * ROW_PITCH + 480 * 4, 4);
    memcpy(blue, pixels + 120 * ROW_PITCH + 320 * 4, 4);
    memcpy(corner, pixels, 4);
    /* BGRA8: RGB vertex colors, with a dark-blue clear outside the triangle. */
    ok = red[2] > 180 && red[1] < 80 && red[0] < 80 &&
            green[1] > 180 && green[2] < 80 && green[0] < 80 &&
            blue[0] > 180 && blue[1] < 80 && blue[2] < 80 &&
            corner[2] < 80 && corner[1] < 80 && corner[0] > 20;
    printf("  readback rgb-red=%02x%02x%02x%02x rgb-green=%02x%02x%02x%02x rgb-blue=%02x%02x%02x%02x clear=%02x%02x%02x%02x: %s\n",
            red[0], red[1], red[2], red[3],
            green[0], green[1], green[2], green[3],
            blue[0], blue[1], blue[2], blue[3],
            corner[0], corner[1], corner[2], corner[3], ok ? "PASS" : "FAIL");
    readback->lpVtbl->Unmap(readback, 0, NULL);
    return ok;
}


#define LC_WIDTH 640
#define LC_HEIGHT 480
#define LC_FORMAT DXGI_FORMAT_B8G8R8A8_UNORM

typedef struct Lifecycle {
    IDXGISwapChain3 *swapchain;
    ID3D12Resource *buffers[BUFFER_COUNT];
    ID3D12DescriptorHeap *rtv_heap;
    ID3D12Resource *readback;
    UINT width;
    UINT height;
    UINT row_pitch;
    UINT flags;
} Lifecycle;

static int accepted_unsupported(HRESULT hr)
{
    return hr == DXGI_ERROR_UNSUPPORTED || hr == E_NOTIMPL ||
            hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || hr == DXGI_ERROR_INVALID_CALL;
}

static void lifecycle_release_buffers(Lifecycle *lc)
{
    UINT i;
    for (i = 0; i < BUFFER_COUNT; i++) {
        if (lc->buffers[i]) ID3D12Resource_Release(lc->buffers[i]);
        lc->buffers[i] = NULL;
    }
    if (lc->rtv_heap) ID3D12DescriptorHeap_Release(lc->rtv_heap);
    if (lc->readback) ID3D12Resource_Release(lc->readback);
    lc->rtv_heap = NULL;
    lc->readback = NULL;
}

static int lifecycle_check_desc(Lifecycle *lc, UINT expected_width, UINT expected_height)
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    HRESULT hr = lc->swapchain->lpVtbl->GetDesc1(lc->swapchain, &desc);
    if (FAILED(hr)) {
        printf("  GetDesc1: FAIL %s\n", hr_text(hr));
        return 0;
    }
    printf("  desc: %ux%u format=0x%x buffers=%u %s\n", desc.Width, desc.Height,
            desc.Format, desc.BufferCount,
            desc.Width == expected_width && desc.Height == expected_height &&
            desc.Format == LC_FORMAT && desc.BufferCount == BUFFER_COUNT ? "PASS" : "FAIL");
    return desc.Width == expected_width && desc.Height == expected_height &&
            desc.Format == LC_FORMAT && desc.BufferCount == BUFFER_COUNT;
}

static int lifecycle_setup_buffers(Runtime *runtime, Lifecycle *lc,
        UINT width, UINT height)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC readback_desc;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    UINT descriptor_size;
    UINT i;
    HRESULT hr;

    if (!width || !height || width > 4096 || height > 4096) {
        printf("  buffer setup %ux%u: UNSUPPORTED\n", width, height);
        return 0;
    }
    lc->width = width;
    lc->height = height;
    lc->row_pitch = (width * 4 + 255u) & ~255u;

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = BUFFER_COUNT;
    hr = runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&lc->rtv_heap);
    if (FAILED(hr)) {
        printf("  RTV heap %ux%u: FAIL %s\n", width, height, hr_text(hr));
        return 0;
    }
    lc->rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(lc->rtv_heap, &start);
    descriptor_size = runtime->device->lpVtbl->GetDescriptorHandleIncrementSize(runtime->device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    memset(&readback_desc, 0, sizeof(readback_desc));
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = (UINT64)lc->row_pitch * height;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&lc->readback);
    if (FAILED(hr)) {
        printf("  readback %ux%u: FAIL %s\n", width, height, hr_text(hr));
        lifecycle_release_buffers(lc);
        return 0;
    }

    for (i = 0; i < BUFFER_COUNT; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = start;
        handle.ptr += (SIZE_T)i * descriptor_size;
        hr = lc->swapchain->lpVtbl->GetBuffer(lc->swapchain, i, &IID_ID3D12Resource,
                (void **)&lc->buffers[i]);
        if (FAILED(hr)) {
            printf("  GetBuffer[%u]: FAIL %s\n", i, hr_text(hr));
            lifecycle_release_buffers(lc);
            return 0;
        }
        runtime->device->lpVtbl->CreateRenderTargetView(runtime->device,
                lc->buffers[i], NULL, handle);
    }
    if (!lifecycle_check_desc(lc, width, height)) {
        lifecycle_release_buffers(lc);
        return 0;
    }
    return 1;
}

static int lifecycle_create_swapchain(Runtime *runtime, Lifecycle *lc,
        UINT width, UINT height)
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    IDXGISwapChain1 *swapchain1 = NULL;
    HRESULT hr;

    memset(lc, 0, sizeof(*lc));
    lc->flags = runtime->tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.Format = LC_FORMAT;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = BUFFER_COUNT;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = lc->flags;
    hr = runtime->factory2->lpVtbl->CreateSwapChainForHwnd(runtime->factory2,
            (IUnknown *)runtime->queue, runtime->window, &desc, NULL, NULL, &swapchain1);
    printf("  create lifecycle swapchain: hr=%s %s\n", hr_text(hr), SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (FAILED(hr)) return 0;
    hr = swapchain1->lpVtbl->QueryInterface(swapchain1, &IID_IDXGISwapChain3,
            (void **)&lc->swapchain);
    IDXGISwapChain1_Release(swapchain1);
    if (FAILED(hr)) {
        printf("  swapchain3: FAIL %s\n", hr_text(hr));
        return 0;
    }
    if (!lifecycle_setup_buffers(runtime, lc, width, height)) {
        IDXGISwapChain3_Release(lc->swapchain);
        lc->swapchain = NULL;
        return 0;
    }
    return 1;
}

static int lifecycle_verify_pixels(Lifecycle *lc, int verbose)
{
    D3D12_RANGE range = { 0, (SIZE_T)lc->row_pitch * lc->height };
    unsigned char *pixels = NULL;
    unsigned char red[4], green[4], blue[4], clear[4];
    UINT red_x = lc->width / 4, green_x = (lc->width * 3) / 4;
    UINT bottom_y = (lc->height * 3) / 4, blue_y = lc->height / 4;
    int ok;
    if (FAILED(lc->readback->lpVtbl->Map(lc->readback, 0, &range, (void **)&pixels)) || !pixels)
        return 0;
    memcpy(red, pixels + bottom_y * lc->row_pitch + red_x * 4, 4);
    memcpy(green, pixels + bottom_y * lc->row_pitch + green_x * 4, 4);
    memcpy(blue, pixels + blue_y * lc->row_pitch + (lc->width / 2) * 4, 4);
    memcpy(clear, pixels, 4);
    ok = red[2] > 180 && red[1] < 80 && red[0] < 80 &&
            green[1] > 180 && green[2] < 80 && green[0] < 80 &&
            blue[0] > 180 && blue[1] < 80 && blue[2] < 80 &&
            clear[2] < 80 && clear[1] < 80 && clear[0] > 20;
    if (verbose)
        printf("  readback %ux%u rgb-red=%02x%02x%02x rgb-green=%02x%02x%02x rgb-blue=%02x%02x%02x clear=%02x%02x%02x: %s\n",
                lc->width, lc->height, red[0], red[1], red[2], green[0], green[1], green[2],
                blue[0], blue[1], blue[2], clear[0], clear[1], clear[2], ok ? "PASS" : "FAIL");
    lc->readback->lpVtbl->Unmap(lc->readback, 0, NULL);
    return ok;
}

static int lifecycle_render(Runtime *runtime, Lifecycle *lc, int verbose)
{
    UINT index = lc->swapchain->lpVtbl->GetCurrentBackBufferIndex(lc->swapchain);
    D3D12_CPU_DESCRIPTOR_HANDLE start, handle;
    D3D12_RESOURCE_BARRIER barrier;
    D3D12_VIEWPORT viewport = { 0, 0, 0, 0, 0, 1 };
    D3D12_RECT scissor = { 0, 0, 0, 0 };
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    UINT descriptor_size;
    float clear_color[4] = { 0.02f, 0.02f, 0.20f, 1.0f };
    HRESULT hr;

    if (index >= BUFFER_COUNT) return 0;
    lc->rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(lc->rtv_heap, &start);
    descriptor_size = runtime->device->lpVtbl->GetDescriptorHandleIncrementSize(runtime->device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    handle = start;
    handle.ptr += (SIZE_T)index * descriptor_size;
    runtime->allocator->lpVtbl->Reset(runtime->allocator);
    runtime->list->lpVtbl->Reset(runtime->list, runtime->allocator, runtime->pso);
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = lc->buffers[index];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    runtime->list->lpVtbl->OMSetRenderTargets(runtime->list, 1, &handle, FALSE, NULL);
    runtime->list->lpVtbl->ClearRenderTargetView(runtime->list, handle, clear_color, 0, NULL);
    viewport.Width = (FLOAT)lc->width;
    viewport.Height = (FLOAT)lc->height;
    scissor.right = (LONG)lc->width;
    scissor.bottom = (LONG)lc->height;
    runtime->list->lpVtbl->RSSetViewports(runtime->list, 1, &viewport);
    runtime->list->lpVtbl->RSSetScissorRects(runtime->list, 1, &scissor);
    runtime->list->lpVtbl->IASetPrimitiveTopology(runtime->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    runtime->list->lpVtbl->DrawInstanced(runtime->list, 3, 1, 0, 0);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    memset(&dst, 0, sizeof(dst));
    dst.pResource = lc->readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = LC_FORMAT;
    dst.PlacedFootprint.Footprint.Width = lc->width;
    dst.PlacedFootprint.Footprint.Height = lc->height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = lc->row_pitch;
    memset(&src, 0, sizeof(src));
    src.pResource = lc->buffers[index];
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    runtime->list->lpVtbl->CopyTextureRegion(runtime->list, &dst, 0, 0, 0, &src, NULL);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    if (FAILED(runtime->list->lpVtbl->Close(runtime->list))) return 0;
    {
        ID3D12CommandList *lists[] = { (ID3D12CommandList *)runtime->list };
        runtime->queue->lpVtbl->ExecuteCommandLists(runtime->queue, 1, lists);
    }
    if (!signal_and_wait(runtime)) return 0;
    if (!lifecycle_verify_pixels(lc, verbose)) return 0;
    hr = lc->swapchain->lpVtbl->Present(lc->swapchain, 0,
            runtime->tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (verbose) printf("  Present after lifecycle event: hr=%s %s\n", hr_text(hr), SUCCEEDED(hr) ? "PASS" : "FAIL");
    return SUCCEEDED(hr);
}

static int lifecycle_resize(Runtime *runtime, Lifecycle *lc, UINT width, UINT height)
{
    HRESULT hr;
    pump_messages();
    if (!signal_and_wait(runtime)) return 0;
    lifecycle_release_buffers(lc);
    hr = lc->swapchain->lpVtbl->ResizeBuffers(lc->swapchain, BUFFER_COUNT, width, height,
            LC_FORMAT, lc->flags);
    printf("  ResizeBuffers(%u,%u): hr=%s %s\n", width, height, hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (FAILED(hr)) return 0;
    return lifecycle_setup_buffers(runtime, lc, width, height);
}

static int lifecycle_invalid_tests(Runtime *runtime, Lifecycle *lc)
{
    HRESULT hr;
    ID3D12Resource *held = NULL;
    IDXGIOutput *output = NULL;
    DXGI_MODE_DESC mode;
    int failures = 0;

    if (!signal_and_wait(runtime)) return 1;
    lifecycle_release_buffers(lc);
    hr = lc->swapchain->lpVtbl->ResizeBuffers(lc->swapchain, BUFFER_COUNT,
            0xffffffffu, 0xffffffffu, LC_FORMAT, lc->flags);
    printf("  invalid resize dimensions: hr=%s %s\n", hr_text(hr), FAILED(hr) ? "PASS" : "FAIL");
    if (!FAILED(hr)) failures++;
    /* Restore a valid descriptor before reacquiring buffers.  Some DXGI
     * implementations retain the requested descriptor while reporting the
     * validation error, so recovery must itself be exercised explicitly. */
    hr = lc->swapchain->lpVtbl->ResizeBuffers(lc->swapchain, BUFFER_COUNT,
            LC_WIDTH, LC_HEIGHT, LC_FORMAT, lc->flags);
    printf("  recover after invalid resize: hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (FAILED(hr)) failures++;
    if (!lifecycle_setup_buffers(runtime, lc, LC_WIDTH, LC_HEIGHT)) return failures + 1;

    held = lc->buffers[0];
    ID3D12Resource_AddRef(held);
    ID3D12Resource_Release(lc->buffers[1]);
    lc->buffers[1] = NULL;
    if (lc->rtv_heap) ID3D12DescriptorHeap_Release(lc->rtv_heap);
    if (lc->readback) ID3D12Resource_Release(lc->readback);
    lc->rtv_heap = NULL;
    lc->readback = NULL;
    hr = lc->swapchain->lpVtbl->ResizeBuffers(lc->swapchain, BUFFER_COUNT,
            LC_WIDTH, LC_HEIGHT, LC_FORMAT, lc->flags);
    printf("  resize with outstanding backbuffer reference: hr=%s %s\n", hr_text(hr),
            FAILED(hr) ? "PASS" : "FAIL");
    if (!FAILED(hr)) failures++;
    ID3D12Resource_Release(held);
    ID3D12Resource_Release(lc->buffers[0]);
    lc->buffers[0] = NULL;
    if (SUCCEEDED(hr)) {
        /* The implementation unexpectedly accepted the outstanding reference;
         * recover the chain before continuing the deterministic test. */
        lifecycle_release_buffers(lc);
    }
    if (!lifecycle_setup_buffers(runtime, lc, LC_WIDTH, LC_HEIGHT)) return failures + 1;

    hr = lc->swapchain->lpVtbl->ResizeTarget(lc->swapchain, NULL);
    printf("  invalid ResizeTarget(NULL): hr=%s %s\n", hr_text(hr), FAILED(hr) ? "PASS" : "FAIL");
    if (!FAILED(hr)) failures++;
    if (SUCCEEDED(runtime->adapter->lpVtbl->EnumOutputs(runtime->adapter, 0, &output))) {
        hr = lc->swapchain->lpVtbl->SetFullscreenState(lc->swapchain, FALSE, output);
        printf("  invalid windowed fullscreen target: hr=%s %s\n", hr_text(hr),
                FAILED(hr) ? "PASS" : "FAIL");
        if (!FAILED(hr)) failures++;
        IDXGIOutput_Release(output);
    } else {
        printf("  invalid windowed fullscreen target: SKIPPED (no output)\n");
    }

    memset(&mode, 0, sizeof(mode));
    mode.Width = 1;
    mode.Height = 1;
    mode.Format = LC_FORMAT;
    mode.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    mode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    hr = lc->swapchain->lpVtbl->ResizeTarget(lc->swapchain, &mode);
    printf("  valid ResizeTarget(1,1): hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : (accepted_unsupported(hr) ? "UNSUPPORTED" : "FAIL"));
    if (FAILED(hr) && !accepted_unsupported(hr)) failures++;
    return failures;
}

static int lifecycle_minimize_restore(Runtime *runtime, Lifecycle *lc)
{
    HRESULT hr;
    int failures = 0;
    ShowWindow(runtime->window, SW_MINIMIZE);
    pump_messages();
    hr = lc->swapchain->lpVtbl->Present(lc->swapchain, 0, DXGI_PRESENT_TEST);
    printf("  minimized occlusion/test Present: hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED ? "PASS" : "FAIL");
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) failures++;

    if (!signal_and_wait(runtime)) return failures + 1;
    lifecycle_release_buffers(lc);
    hr = lc->swapchain->lpVtbl->ResizeBuffers(lc->swapchain, 0, 0, 0,
            DXGI_FORMAT_UNKNOWN, lc->flags);
    printf("  minimized ResizeBuffers(0,0): hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "SUPPORTED" : (accepted_unsupported(hr) ? "UNSUPPORTED" : "FAIL"));
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC1 desc;
        lc->swapchain->lpVtbl->GetDesc1(lc->swapchain, &desc);
        if (desc.Width && desc.Height) {
            if (!lifecycle_setup_buffers(runtime, lc, desc.Width, desc.Height) ||
                    !lifecycle_render(runtime, lc, 1)) failures++;
        } else {
            printf("  minimized dimensions: UNSUPPORTED (zero drawable)\n");
        }
    } else if (!accepted_unsupported(hr)) {
        failures++;
    }

    ShowWindow(runtime->window, SW_RESTORE);
    SetWindowPos(runtime->window, NULL, 100, 100, LC_WIDTH, LC_HEIGHT,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    pump_messages();
    if (!lc->readback) {
        /* A supported zero-size resize intentionally leaves no drawable.
         * Restore the normal descriptor before reacquiring resources rather
         * than treating the expected 0x0 description as a failed setup. */
        hr = lc->swapchain->lpVtbl->ResizeBuffers(lc->swapchain, BUFFER_COUNT,
                LC_WIDTH, LC_HEIGHT, LC_FORMAT, lc->flags);
        printf("  restore ResizeBuffers: hr=%s %s\n", hr_text(hr), SUCCEEDED(hr) ? "PASS" : "FAIL");
        if (FAILED(hr) || !lifecycle_setup_buffers(runtime, lc, LC_WIDTH, LC_HEIGHT)) failures++;
    }
    if (lc->readback && !lifecycle_render(runtime, lc, 1)) failures++;
    return failures;
}

static int lifecycle_fullscreen(Runtime *runtime, Lifecycle *lc)
{
    BOOL fullscreen = FALSE;
    IDXGIOutput *output = NULL;
    DXGI_MODE_DESC mode;
    HRESULT hr;
    int failures = 0;

    hr = lc->swapchain->lpVtbl->GetFullscreenState(lc->swapchain, &fullscreen, &output);
    printf("  GetFullscreenState(initial): hr=%s windowed=%u %s\n", hr_text(hr), !fullscreen,
            SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (output) IDXGIOutput_Release(output);
    if (FAILED(hr)) failures++;

    hr = lc->swapchain->lpVtbl->SetFullscreenState(lc->swapchain, TRUE, NULL);
    printf("  SetFullscreenState(TRUE): hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "SUPPORTED" : (accepted_unsupported(hr) ? "UNSUPPORTED" : "FAIL"));
    if (FAILED(hr) && !accepted_unsupported(hr)) failures++;
    if (SUCCEEDED(hr)) {
        fullscreen = FALSE;
        hr = lc->swapchain->lpVtbl->GetFullscreenState(lc->swapchain, &fullscreen, NULL);
        printf("  GetFullscreenState(after TRUE): hr=%s fullscreen=%u %s\n", hr_text(hr), fullscreen,
                SUCCEEDED(hr) && fullscreen ? "PASS" : "FAIL");
        if (FAILED(hr) || !fullscreen) failures++;
    }

    memset(&mode, 0, sizeof(mode));
    mode.Width = LC_WIDTH;
    mode.Height = LC_HEIGHT;
    mode.Format = LC_FORMAT;
    mode.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    mode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    hr = lc->swapchain->lpVtbl->ResizeTarget(lc->swapchain, &mode);
    printf("  ResizeTarget(lifecycle): hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : (accepted_unsupported(hr) ? "UNSUPPORTED" : "FAIL"));
    if (FAILED(hr) && !accepted_unsupported(hr)) failures++;

    hr = lc->swapchain->lpVtbl->SetFullscreenState(lc->swapchain, FALSE, NULL);
    printf("  SetFullscreenState(FALSE) fallback: hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (FAILED(hr)) failures++;
    return failures;
}

static int lifecycle_destroy_recreate(Runtime *runtime, Lifecycle *lc)
{
    HRESULT hr;
    HWND old_window = runtime->window;
    int failures = 0;
    if (!signal_and_wait(runtime)) return 1;
    lifecycle_release_buffers(lc);
    DestroyWindow(old_window);
    runtime->window = NULL;
    hr = lc->swapchain->lpVtbl->Present(lc->swapchain, 0, DXGI_PRESENT_TEST);
    printf("  Present after window destruction: hr=%s %s\n", hr_text(hr),
            SUCCEEDED(hr) || accepted_unsupported(hr) ? "PASS" : "FAIL");
    if (FAILED(hr) && !accepted_unsupported(hr)) failures++;
    lc->swapchain->lpVtbl->Release(lc->swapchain);
    lc->swapchain = NULL;

    runtime->window = create_window();
    if (!runtime->window) return failures + 1;
    runtime->factory->lpVtbl->MakeWindowAssociation(runtime->factory, runtime->window,
            DXGI_MWA_NO_ALT_ENTER);
    if (!lifecycle_create_swapchain(runtime, lc, LC_WIDTH, LC_HEIGHT) ||
            !lifecycle_render(runtime, lc, 1)) failures++;
    return failures;
}

static int lifecycle_cycles(Runtime *runtime)
{
    unsigned int cycle;
    for (cycle = 1; cycle <= 100; cycle++) {
        Lifecycle lc;
        if (!lifecycle_create_swapchain(runtime, &lc, 320 + (cycle % 4) * 64,
                240 + (cycle % 3) * 48)) {
            printf("  cycle %u: FAIL create\n", cycle);
            return 0;
        }
        if (!lifecycle_render(runtime, &lc, cycle == 1)) {
            printf("  cycle %u: FAIL render\n", cycle);
            lifecycle_release_buffers(&lc);
            lc.swapchain->lpVtbl->Release(lc.swapchain);
            return 0;
        }
        if (!lifecycle_resize(runtime, &lc, 640 + (cycle % 3) * 64,
                360 + (cycle % 2) * 48) || !lifecycle_render(runtime, &lc, 0)) {
            printf("  cycle %u: FAIL resize/render\n", cycle);
            lifecycle_release_buffers(&lc);
            lc.swapchain->lpVtbl->Release(lc.swapchain);
            return 0;
        }
        lifecycle_release_buffers(&lc);
        lc.swapchain->lpVtbl->Release(lc.swapchain);
        if ((cycle % 25) == 0) printf("  create/resize/destroy cycles: %u / 100\n", cycle);
    }
    return 1;
}

static void lifecycle_shutdown(Runtime *runtime, Lifecycle *lc)
{
    printf("  shutdown: GPU idle\n");
    signal_and_wait(runtime);
    printf("  shutdown: RTVs/resources\n");
    lifecycle_release_buffers(lc);
    printf("  shutdown: swapchain\n");
    if (lc->swapchain) lc->swapchain->lpVtbl->Release(lc->swapchain);
    lc->swapchain = NULL;
    printf("  shutdown: queue/fence\n");
    if (runtime->queue) ID3D12CommandQueue_Release(runtime->queue);
    runtime->queue = NULL;
    if (runtime->fence) ID3D12Fence_Release(runtime->fence);
    runtime->fence = NULL;
    if (runtime->fence_event) CloseHandle(runtime->fence_event);
    runtime->fence_event = NULL;
    if (runtime->pso) ID3D12PipelineState_Release(runtime->pso);
    runtime->pso = NULL;
    if (runtime->root_signature) ID3D12RootSignature_Release(runtime->root_signature);
    runtime->root_signature = NULL;
    if (runtime->list) ID3D12GraphicsCommandList_Release(runtime->list);
    runtime->list = NULL;
    if (runtime->allocator) ID3D12CommandAllocator_Release(runtime->allocator);
    runtime->allocator = NULL;
    printf("  shutdown: device\n");
    if (runtime->device) ID3D12Device_Release(runtime->device);
    runtime->device = NULL;
    printf("  shutdown: adapter\n");
    if (runtime->adapter) IDXGIAdapter1_Release(runtime->adapter);
    runtime->adapter = NULL;
    printf("  shutdown: factory\n");
    if (runtime->factory2) IDXGIFactory2_Release(runtime->factory2);
    runtime->factory2 = NULL;
    if (runtime->factory) IDXGIFactory1_Release(runtime->factory);
    runtime->factory = NULL;
    printf("  shutdown: window\n");
    if (runtime->window) DestroyWindow(runtime->window);
    runtime->window = NULL;
}

int main(void)
{
    Runtime runtime;
    Lifecycle lc;
    int failures = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== DXGI-3 window lifecycle probe (RGB triangle) ===\n");
    printf("resize, minimize, occlusion, fullscreen, destruction, recreation, and 100-cycle stress\n");
    if (!create_runtime(&runtime)) {
        release_runtime(&runtime);
        return 1;
    }
    if (!lifecycle_create_swapchain(&runtime, &lc, LC_WIDTH, LC_HEIGHT) ||
            !lifecycle_render(&runtime, &lc, 1)) failures++;
    if (!failures) {
        printf("\n=== resize matrix ===\n");
        if (!lifecycle_resize(&runtime, &lc, 800, 600) || !lifecycle_render(&runtime, &lc, 1)) failures++;
        if (!lifecycle_resize(&runtime, &lc, 320, 240) || !lifecycle_render(&runtime, &lc, 1)) failures++;
        if (!lifecycle_resize(&runtime, &lc, 1024, 512) || !lifecycle_render(&runtime, &lc, 1)) failures++;
    }
    if (!failures) {
        printf("\n=== deterministic negative lifecycle tests ===\n");
        failures += lifecycle_invalid_tests(&runtime, &lc);
    }
    if (!failures) {
        printf("\n=== minimize/restore and occlusion ===\n");
        failures += lifecycle_minimize_restore(&runtime, &lc);
    }
    if (!failures) {
        printf("\n=== fullscreen and windowed fallback ===\n");
        failures += lifecycle_fullscreen(&runtime, &lc);
    }
    if (!failures) {
        printf("\n=== window destruction and recreation ===\n");
        failures += lifecycle_destroy_recreate(&runtime, &lc);
    }
    if (!failures) {
        printf("\n=== 100 create/resize/destroy cycles ===\n");
        lifecycle_release_buffers(&lc);
        lc.swapchain->lpVtbl->Release(lc.swapchain);
        lc.swapchain = NULL;
        if (!lifecycle_cycles(&runtime)) failures++;
        if (!lifecycle_create_swapchain(&runtime, &lc, LC_WIDTH, LC_HEIGHT) ||
                !lifecycle_render(&runtime, &lc, 1)) failures++;
    }
    lifecycle_shutdown(&runtime, &lc);
    printf("DXGI-3 result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
            failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
