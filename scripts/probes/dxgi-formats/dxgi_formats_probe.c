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

/* DXGI-4 format/color probe. */
#include <math.h>

#define FORMAT_WIDTH 640
#define FORMAT_HEIGHT 480
#define FORMAT_ROW_PITCH ((FORMAT_WIDTH * 4 + 255u) & ~255u)
#define FORMAT_READBACK_SIZE ((UINT64)FORMAT_ROW_PITCH * FORMAT_HEIGHT)
#define FORMAT_COUNT 2
#define FORMAT_CLEAR_R 0.02f
#define FORMAT_CLEAR_G 0.02f
#define FORMAT_CLEAR_B 0.20f

typedef struct FormatSpec {
    const char *name;
    DXGI_FORMAT format;
    int srgb;
    int bgra;
    int packed10;
} FormatSpec;

typedef struct FormatCase {
    IDXGISwapChain3 *swapchain;
    IDXGISwapChain4 *swapchain4;
    ID3D12Resource *buffers[FORMAT_COUNT];
    ID3D12DescriptorHeap *rtv_heap;
    ID3D12Resource *readback;
    ID3D12PipelineState *pso;
    UINT flags;
} FormatCase;

static const FormatSpec format_specs[] = {
    { "B8G8R8A8_UNORM",       DXGI_FORMAT_B8G8R8A8_UNORM,       0, 1, 0 },
    { "B8G8R8A8_UNORM_SRGB", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, 1, 1, 0 },
    { "R8G8B8A8_UNORM",       DXGI_FORMAT_R8G8B8A8_UNORM,       0, 0, 0 },
    { "R10G10B10A2_UNORM",    DXGI_FORMAT_R10G10B10A2_UNORM,    0, 0, 1 },
};

static int format_unsupported(HRESULT hr)
{
    return hr == DXGI_ERROR_UNSUPPORTED || hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ||
            hr == DXGI_ERROR_INVALID_CALL || hr == E_NOTIMPL || hr == E_INVALIDARG ||
            hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

static float srgb_encode(float value)
{
    if (value <= 0.0031308f) return value * 12.92f;
    return 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
}

static int near_u(unsigned int actual, unsigned int expected, unsigned int tolerance)
{
    return actual > expected ? actual - expected <= tolerance : expected - actual <= tolerance;
}

static void format_release_buffers(FormatCase *fc)
{
    UINT i;
    for (i = 0; i < FORMAT_COUNT; i++) {
        if (fc->buffers[i]) ID3D12Resource_Release(fc->buffers[i]);
        fc->buffers[i] = NULL;
    }
    if (fc->rtv_heap) ID3D12DescriptorHeap_Release(fc->rtv_heap);
    if (fc->readback) ID3D12Resource_Release(fc->readback);
    fc->rtv_heap = NULL;
    fc->readback = NULL;
}

static void format_release_case(FormatCase *fc)
{
    format_release_buffers(fc);
    if (fc->pso) ID3D12PipelineState_Release(fc->pso);
    if (fc->swapchain4) IDXGISwapChain4_Release(fc->swapchain4);
    else if (fc->swapchain) IDXGISwapChain3_Release(fc->swapchain);
    memset(fc, 0, sizeof(*fc));
}

static int create_format_pso(Runtime *runtime, DXGI_FORMAT format, DXGI_FORMAT dsv_format,
        ID3D12PipelineState **out_pso)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC_MS desc;
    HRESULT hr;
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
    desc.RTVFormats[0] = format;
    desc.DSVFormat = dsv_format;
    desc.SampleDesc.Count = 1;
    if (dsv_format != DXGI_FORMAT_UNKNOWN) {
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        desc.DepthStencilState.StencilEnable = TRUE;
        desc.DepthStencilState.StencilReadMask = 0xff;
        desc.DepthStencilState.StencilWriteMask = 0xff;
        desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
        desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
    }
    hr = runtime->device->lpVtbl->CreateGraphicsPipelineState(runtime->device,
            (const D3D12_GRAPHICS_PIPELINE_STATE_DESC *)(const void *)&desc,
            &IID_ID3D12PipelineState, (void **)out_pso);
    return SUCCEEDED(hr);
}

static int query_format(Runtime *runtime, const FormatSpec *spec)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support;
    HRESULT hr;
    memset(&support, 0, sizeof(support));
    support.Format = spec->format;
    hr = runtime->device->lpVtbl->CheckFeatureSupport(runtime->device,
            D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    printf("  format support %-24s: hr=%s support1=0x%08x support2=0x%08x %s\n",
            spec->name, hr_text(hr), support.Support1, support.Support2,
            SUCCEEDED(hr) ? "SUPPORTED" : (format_unsupported(hr) ? "UNSUPPORTED" : "FAIL"));
    return SUCCEEDED(hr);
}

static int format_setup_buffers(Runtime *runtime, FormatCase *fc, const FormatSpec *spec)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC readback_desc;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    UINT descriptor_size, i;
    DXGI_SWAP_CHAIN_DESC1 desc;
    HRESULT hr;
    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = FORMAT_COUNT;
    hr = runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&fc->rtv_heap);
    if (FAILED(hr)) return 0;
    fc->rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(fc->rtv_heap, &start);
    descriptor_size = runtime->device->lpVtbl->GetDescriptorHandleIncrementSize(runtime->device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    memset(&readback_desc, 0, sizeof(readback_desc));
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = FORMAT_READBACK_SIZE;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&fc->readback);
    if (FAILED(hr)) { format_release_buffers(fc); return 0; }
    for (i = 0; i < FORMAT_COUNT; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = start;
        handle.ptr += (SIZE_T)i * descriptor_size;
        hr = fc->swapchain->lpVtbl->GetBuffer(fc->swapchain, i, &IID_ID3D12Resource,
                (void **)&fc->buffers[i]);
        if (FAILED(hr)) { format_release_buffers(fc); return 0; }
        runtime->device->lpVtbl->CreateRenderTargetView(runtime->device,
                fc->buffers[i], NULL, handle);
    }
    memset(&desc, 0, sizeof(desc));
    hr = fc->swapchain->lpVtbl->GetDesc1(fc->swapchain, &desc);
    printf("  format desc %-24s: hr=%s %ux%u format=0x%x buffers=%u alpha=%u %s\n",
            spec->name, hr_text(hr), desc.Width, desc.Height, desc.Format, desc.BufferCount,
            desc.AlphaMode, SUCCEEDED(hr) && desc.Width == FORMAT_WIDTH &&
            desc.Height == FORMAT_HEIGHT && desc.Format == spec->format &&
            desc.BufferCount == FORMAT_COUNT ? "PASS" : "FAIL");
    return SUCCEEDED(hr) && desc.Width == FORMAT_WIDTH && desc.Height == FORMAT_HEIGHT &&
            desc.Format == spec->format && desc.BufferCount == FORMAT_COUNT;
}

static int create_format_swapchain(Runtime *runtime, FormatCase *fc, const FormatSpec *spec)
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    IDXGISwapChain1 *swapchain1 = NULL;
    HRESULT hr;
    memset(fc, 0, sizeof(*fc));
    fc->flags = runtime->tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    memset(&desc, 0, sizeof(desc));
    desc.Width = FORMAT_WIDTH;
    desc.Height = FORMAT_HEIGHT;
    desc.Format = spec->format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FORMAT_COUNT;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = fc->flags;
    hr = runtime->factory2->lpVtbl->CreateSwapChainForHwnd(runtime->factory2,
            (IUnknown *)runtime->queue, runtime->window, &desc, NULL, NULL, &swapchain1);
    printf("  swapchain %-24s: hr=%s %s\n", spec->name, hr_text(hr),
            SUCCEEDED(hr) ? "SUPPORTED" : (format_unsupported(hr) ? "UNSUPPORTED" : "FAIL"));
    if (FAILED(hr)) return format_unsupported(hr) ? 2 : 0;
    hr = swapchain1->lpVtbl->QueryInterface(swapchain1, &IID_IDXGISwapChain3,
            (void **)&fc->swapchain);
    if (FAILED(hr)) { IDXGISwapChain1_Release(swapchain1); return 0; }
    hr = swapchain1->lpVtbl->QueryInterface(swapchain1, &IID_IDXGISwapChain4,
            (void **)&fc->swapchain4);
    IDXGISwapChain1_Release(swapchain1);
    if (FAILED(hr)) fc->swapchain4 = NULL;
    if (!format_setup_buffers(runtime, fc, spec)) { format_release_case(fc); return 0; }
    if (!create_format_pso(runtime, spec->format, DXGI_FORMAT_UNKNOWN, &fc->pso)) {
        printf("  format PSO %-24s: FAIL\n", spec->name);
        format_release_case(fc);
        return 0;
    }
    return 1;
}

static int create_offscreen_format(Runtime *runtime, FormatCase *fc, const FormatSpec *spec)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC resource_desc, readback_desc;
    D3D12_CLEAR_VALUE clear;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    HRESULT hr;
    memset(fc, 0, sizeof(*fc));
    memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = FORMAT_WIDTH; resource_desc.Height = FORMAT_HEIGHT;
    resource_desc.DepthOrArraySize = 1; resource_desc.MipLevels = 1;
    resource_desc.Format = spec->format; resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    memset(&clear, 0, sizeof(clear)); clear.Format = spec->format;
    clear.Color[0] = FORMAT_CLEAR_R; clear.Color[1] = FORMAT_CLEAR_G;
    clear.Color[2] = FORMAT_CLEAR_B; clear.Color[3] = 1.0f;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clear, &IID_ID3D12Resource, (void **)&fc->buffers[0]);
    if (FAILED(hr)) {
        printf("  offscreen %-24s: hr=%s UNSUPPORTED\n", spec->name, hr_text(hr));
        return format_unsupported(hr) ? 2 : 0;
    }
    memset(&heap_desc, 0, sizeof(heap_desc)); heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap_desc.NumDescriptors = 1;
    hr = runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&fc->rtv_heap);
    if (FAILED(hr)) { format_release_case(fc); return 0; }
    fc->rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(fc->rtv_heap, &handle);
    runtime->device->lpVtbl->CreateRenderTargetView(runtime->device, fc->buffers[0], NULL, handle);
    memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_READBACK;
    memset(&readback_desc, 0, sizeof(readback_desc)); readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = FORMAT_READBACK_SIZE; readback_desc.Height = 1; readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1; readback_desc.SampleDesc.Count = 1; readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&fc->readback);
    if (FAILED(hr) || !create_format_pso(runtime, spec->format, DXGI_FORMAT_UNKNOWN, &fc->pso)) {
        format_release_case(fc); return 0;
    }
    /* The native DXGI lane rejects this format for presentation. The backend
     * advertises the D3D12 format bits and can create the resource/RTV, but a
     * GPU copy/readback submission is not safe on this lane (it can block in
     * the Metal format conversion path). Report that capability boundary
     * explicitly instead of risking a hang or synthesizing pixels. */
    printf("  offscreen %-24s: resource/RTV SUPPORTED; GPU render/readback UNSUPPORTED\n", spec->name);
    format_release_case(fc);
    return 3;
}

static void decode_pixel(const FormatSpec *spec, const unsigned char *p,
        unsigned int *r, unsigned int *g, unsigned int *b, unsigned int *a)
{
    if (spec->packed10) {
        UINT32 value = (UINT32)p[0] | ((UINT32)p[1] << 8) |
                ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
        *r = value & 0x3ff;
        *g = (value >> 10) & 0x3ff;
        *b = (value >> 20) & 0x3ff;
        *a = (value >> 30) & 0x3;
    } else if (spec->bgra) {
        *b = p[0]; *g = p[1]; *r = p[2]; *a = p[3];
    } else {
        *r = p[0]; *g = p[1]; *b = p[2]; *a = p[3];
    }
}

static int verify_format_pixels(FormatCase *fc, const FormatSpec *spec)
{
    D3D12_RANGE range = { 0, FORMAT_READBACK_SIZE };
    unsigned char *pixels = NULL;
    unsigned int r, g, b, a, cr, cg, cb, ca;
    float expected_r = 191.0f / 255.0f;
    float expected_g = 22.0f / 255.0f;
    float expected_b = 42.0f / 255.0f;
    float clear_r = FORMAT_CLEAR_R, clear_g = FORMAT_CLEAR_G, clear_b = FORMAT_CLEAR_B;
    UINT x_red = FORMAT_WIDTH / 4, x_green = FORMAT_WIDTH * 3 / 4;
    UINT y_bottom = FORMAT_HEIGHT * 3 / 4, y_blue = FORMAT_HEIGHT / 4;
    unsigned int scale = spec->packed10 ? 1023 : 255;
    unsigned int tolerance = spec->packed10 ? 10 : 5;
    unsigned char *red, *green, *blue, *clear;
    int ok;
    if (FAILED(fc->readback->lpVtbl->Map(fc->readback, 0, &range, (void **)&pixels)) || !pixels)
        return 0;
    red = pixels + y_bottom * FORMAT_ROW_PITCH + x_red * 4;
    green = pixels + y_bottom * FORMAT_ROW_PITCH + x_green * 4;
    blue = pixels + y_blue * FORMAT_ROW_PITCH + (FORMAT_WIDTH / 2) * 4;
    clear = pixels;
    decode_pixel(spec, red, &r, &g, &b, &a);
    printf("  readback %-24s red=%u,%u,%u,%u ", spec->name, r, g, b, a);
    decode_pixel(spec, green, &cr, &cg, &cb, &ca);
    printf("green=%u,%u,%u,%u ", cr, cg, cb, ca);
    decode_pixel(spec, blue, &cr, &cg, &cb, &ca);
    printf("blue=%u,%u,%u,%u ", cr, cg, cb, ca);
    decode_pixel(spec, clear, &cr, &cg, &cb, &ca);
    printf("clear=%u,%u,%u,%u", cr, cg, cb, ca);
    if (spec->srgb) {
        expected_r = srgb_encode(expected_r);
        expected_g = srgb_encode(expected_g);
        expected_b = srgb_encode(expected_b);
        clear_r = srgb_encode(clear_r);
        clear_g = srgb_encode(clear_g);
        clear_b = srgb_encode(clear_b);
    }
    decode_pixel(spec, green, &cr, &cg, &cb, &ca);
    ok = near_u(r, (unsigned int)lroundf(expected_r * scale), tolerance) &&
            near_u(g, (unsigned int)lroundf(expected_g * scale), tolerance) &&
            near_u(b, (unsigned int)lroundf(expected_b * scale), tolerance) &&
            near_u(a, scale, spec->packed10 ? 1 : 3);
    decode_pixel(spec, clear, &cr, &cg, &cb, &ca);
    ok = ok && near_u(cr, (unsigned int)lroundf(clear_r * scale), tolerance) &&
            near_u(cg, (unsigned int)lroundf(clear_g * scale), tolerance) &&
            near_u(cb, (unsigned int)lroundf(clear_b * scale), tolerance) &&
            near_u(ca, scale, spec->packed10 ? 1 : 3);
    fc->readback->lpVtbl->Unmap(fc->readback, 0, NULL);
    printf(" : %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int render_format_case(Runtime *runtime, FormatCase *fc, const FormatSpec *spec)
{
    UINT index = fc->swapchain->lpVtbl->GetCurrentBackBufferIndex(fc->swapchain);
    D3D12_CPU_DESCRIPTOR_HANDLE start, handle;
    D3D12_RESOURCE_BARRIER barrier;
    D3D12_VIEWPORT viewport = { 0, 0, FORMAT_WIDTH, FORMAT_HEIGHT, 0, 1 };
    D3D12_RECT scissor = { 0, 0, FORMAT_WIDTH, FORMAT_HEIGHT };
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    UINT descriptor_size;
    float clear_color[4] = { FORMAT_CLEAR_R, FORMAT_CLEAR_G, FORMAT_CLEAR_B, 1.0f };
    HRESULT hr;
    if (fc->swapchain && index >= FORMAT_COUNT) return 0;
    if (!fc->swapchain) index = 0;
    fc->rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(fc->rtv_heap, &start);
    descriptor_size = runtime->device->lpVtbl->GetDescriptorHandleIncrementSize(runtime->device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    handle = start; handle.ptr += (SIZE_T)index * descriptor_size;
    runtime->allocator->lpVtbl->Reset(runtime->allocator);
    runtime->list->lpVtbl->Reset(runtime->list, runtime->allocator, fc->pso);
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = fc->buffers[index];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = fc->swapchain ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (fc->swapchain) runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    runtime->list->lpVtbl->OMSetRenderTargets(runtime->list, 1, &handle, FALSE, NULL);
    runtime->list->lpVtbl->ClearRenderTargetView(runtime->list, handle, clear_color, 0, NULL);
    runtime->list->lpVtbl->RSSetViewports(runtime->list, 1, &viewport);
    runtime->list->lpVtbl->RSSetScissorRects(runtime->list, 1, &scissor);
    runtime->list->lpVtbl->IASetPrimitiveTopology(runtime->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    runtime->list->lpVtbl->DrawInstanced(runtime->list, 3, 1, 0, 0);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    memset(&dst, 0, sizeof(dst));
    dst.pResource = fc->readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = spec->format;
    dst.PlacedFootprint.Footprint.Width = FORMAT_WIDTH;
    dst.PlacedFootprint.Footprint.Height = FORMAT_HEIGHT;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = FORMAT_ROW_PITCH;
    memset(&src, 0, sizeof(src));
    src.pResource = fc->buffers[index];
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    runtime->list->lpVtbl->CopyTextureRegion(runtime->list, &dst, 0, 0, 0, &src, NULL);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = fc->swapchain ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_RENDER_TARGET;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barrier);
    if (FAILED(runtime->list->lpVtbl->Close(runtime->list))) return 0;
    { ID3D12CommandList *lists[] = { (ID3D12CommandList *)runtime->list };
      runtime->queue->lpVtbl->ExecuteCommandLists(runtime->queue, 1, lists); }
    if (!signal_and_wait(runtime) || !verify_format_pixels(fc, spec)) return 0;
    if (fc->swapchain) {
        hr = fc->swapchain->lpVtbl->Present(fc->swapchain, 0,
                runtime->tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0);
        printf("  present %-24s: hr=%s tearing=%s %s\n", spec->name, hr_text(hr),
                runtime->tearing_supported ? "ALLOW_TEARING" : "NONE", SUCCEEDED(hr) ? "PASS" : "FAIL");
        return SUCCEEDED(hr);
    }
    printf("  present %-24s: offscreen readback PASS\n", spec->name);
    return 1;
}

static int color_space_matrix(Runtime *runtime)
{
    const struct { const char *name; DXGI_COLOR_SPACE_TYPE type; } spaces[] = {
        { "SDR P709", DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 },
        { "scRGB linear", DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 },
        { "HDR10 PQ", DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 },
        { "extended P2020", DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 },
    };
    FormatCase fc;
    UINT i, support;
    HRESULT hr;
    int failures = 0;
    if (create_format_swapchain(runtime, &fc, &format_specs[0]) != 1) return 1;
    printf("\n=== color-space and alpha policy ===\n");
    {
        DXGI_SWAP_CHAIN_DESC1 desc;
        fc.swapchain->lpVtbl->GetDesc1(fc.swapchain, &desc);
        printf("  alpha mode GetDesc1: value=%u %s\n", desc.AlphaMode,
                desc.AlphaMode == DXGI_ALPHA_MODE_IGNORE ? "PASS" : "REPORTED");
    }
    printf("  GetColorSpace1: NOT EXPOSED (IDXGISwapChain3/4 provide Check/Set only)\n");
    for (i = 0; i < sizeof(spaces) / sizeof(spaces[0]); i++) {
        support = 0;
        hr = fc.swapchain4 ? fc.swapchain4->lpVtbl->CheckColorSpaceSupport(fc.swapchain4,
                spaces[i].type, &support) : E_NOINTERFACE;
        printf("  CheckColorSpaceSupport %-16s: hr=%s flags=0x%x %s\n", spaces[i].name,
                hr_text(hr), support, SUCCEEDED(hr) ? (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT ? "SUPPORTED" : "UNSUPPORTED") : "UNSUPPORTED");
        if (fc.swapchain4 && SUCCEEDED(hr) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
            hr = fc.swapchain4->lpVtbl->SetColorSpace1(fc.swapchain4, spaces[i].type);
            printf("  SetColorSpace1 %-24s: hr=%s %s\n", spaces[i].name, hr_text(hr), SUCCEEDED(hr) ? "PASS" : "FAIL");
            if (FAILED(hr)) failures++;
        } else {
            hr = fc.swapchain4 ? fc.swapchain4->lpVtbl->SetColorSpace1(fc.swapchain4, spaces[i].type) : E_NOINTERFACE;
            printf("  SetColorSpace1 %-24s: hr=%s UNSUPPORTED\n", spaces[i].name, hr_text(hr));
            if (SUCCEEDED(hr)) failures++;
        }
    }
    if (fc.swapchain4) {
        DXGI_HDR_METADATA_HDR10 hdr10;
        memset(&hdr10, 0, sizeof(hdr10));
        hr = fc.swapchain4->lpVtbl->SetHDRMetaData(fc.swapchain4,
                DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10), NULL);
        printf("  invalid HDR10 metadata(NULL): hr=%s %s\n", hr_text(hr), FAILED(hr) ? "PASS" : "FAIL");
        if (!FAILED(hr)) failures++;
        hr = fc.swapchain4->lpVtbl->SetHDRMetaData(fc.swapchain4,
                DXGI_HDR_METADATA_TYPE_NONE, 0, NULL);
        printf("  clear HDR metadata: hr=%s %s\n", hr_text(hr), SUCCEEDED(hr) ? "PASS" : "UNSUPPORTED");
        if (FAILED(hr) && !format_unsupported(hr)) failures++;
        hr = fc.swapchain4->lpVtbl->SetHDRMetaData(fc.swapchain4,
                DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10), &hdr10);
        printf("  valid HDR10 metadata update: hr=%s %s\n", hr_text(hr),
                SUCCEEDED(hr) ? "REPORTED" : (format_unsupported(hr) ? "UNSUPPORTED" : "FAIL"));
        if (FAILED(hr) && !format_unsupported(hr)) failures++;
    } else {
        printf("  HDR metadata APIs: UNSUPPORTED (IDXGISwapChain4 unavailable)\n");
    }
    format_release_case(&fc);
    return failures;
}

static int resolve_case(Runtime *runtime)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC msaa_desc, single_desc, readback_desc;
    D3D12_CLEAR_VALUE clear;
    ID3D12Resource *msaa = NULL, *single = NULL, *readback = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    D3D12_RESOURCE_BARRIER barriers[2];
    D3D12_RANGE range = { 0, FORMAT_READBACK_SIZE };
    unsigned char *pixels = NULL;
    unsigned int pixel_b = 0, pixel_g = 0, pixel_r = 0, pixel_a = 0;
    HRESULT hr;
    int ok = 0;
    printf("\n=== deterministic color resolve ===\n");
    memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(&msaa_desc, 0, sizeof(msaa_desc)); msaa_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    msaa_desc.Width = FORMAT_WIDTH; msaa_desc.Height = FORMAT_HEIGHT; msaa_desc.DepthOrArraySize = 1;
    msaa_desc.MipLevels = 1; msaa_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    msaa_desc.SampleDesc.Count = 4; msaa_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    msaa_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    single_desc = msaa_desc; single_desc.SampleDesc.Count = 1;
    memset(&clear, 0, sizeof(clear)); clear.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    clear.Color[0] = FORMAT_CLEAR_R; clear.Color[1] = FORMAT_CLEAR_G;
    clear.Color[2] = FORMAT_CLEAR_B; clear.Color[3] = 1.0f;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &msaa_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clear, &IID_ID3D12Resource, (void **)&msaa);
    if (FAILED(hr)) goto done;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &single_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clear, &IID_ID3D12Resource, (void **)&single);
    if (FAILED(hr)) goto done;
    memset(&heap_desc, 0, sizeof(heap_desc)); heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap_desc.NumDescriptors = 1;
    hr = runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (FAILED(hr)) goto done;
    rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap, &handle);
    runtime->device->lpVtbl->CreateRenderTargetView(runtime->device, msaa, NULL, handle);
    memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_READBACK;
    memset(&readback_desc, 0, sizeof(readback_desc)); readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = FORMAT_READBACK_SIZE; readback_desc.Height = 1; readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1; readback_desc.SampleDesc.Count = 1; readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &heap,
            D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&readback);
    if (FAILED(hr)) goto done;
    runtime->allocator->lpVtbl->Reset(runtime->allocator);
    runtime->list->lpVtbl->Reset(runtime->list, runtime->allocator, runtime->pso);
    runtime->list->lpVtbl->OMSetRenderTargets(runtime->list, 1, &handle, FALSE, NULL);
    runtime->list->lpVtbl->ClearRenderTargetView(runtime->list, handle, clear.Color, 0, NULL);
    memset(barriers, 0, sizeof(barriers));
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = msaa;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    barriers[1] = barriers[0];
    barriers[1].Transition.pResource = single;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 2, barriers);
    runtime->list->lpVtbl->ResolveSubresource(runtime->list, single, 0, msaa, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list, 1, &barriers[1]);
    memset(&dst, 0, sizeof(dst)); dst.pResource = readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = FORMAT_WIDTH; dst.PlacedFootprint.Footprint.Height = FORMAT_HEIGHT;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = FORMAT_ROW_PITCH;
    memset(&src, 0, sizeof(src)); src.pResource = single; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    runtime->list->lpVtbl->CopyTextureRegion(runtime->list, &dst, 0, 0, 0, &src, NULL);
    if (FAILED(runtime->list->lpVtbl->Close(runtime->list))) goto done;
    { ID3D12CommandList *lists[] = { (ID3D12CommandList *)runtime->list };
      runtime->queue->lpVtbl->ExecuteCommandLists(runtime->queue, 1, lists); }
    if (!signal_and_wait(runtime)) goto done;
    if (FAILED(readback->lpVtbl->Map(readback, 0, &range, (void **)&pixels)) || !pixels) goto done;
    ok = near_u(pixels[0], 51, 3) && near_u(pixels[1], 5, 3) &&
            near_u(pixels[2], 5, 3) && pixels[3] == 255;
    pixel_b = pixels[0]; pixel_g = pixels[1]; pixel_r = pixels[2]; pixel_a = pixels[3];
    readback->lpVtbl->Unmap(readback, 0, NULL);
    printf("  ResolveSubresource B8G8R8A8_UNORM: clear BGRA=%u,%u,%u,%u %s\n",
            pixel_b, pixel_g, pixel_r, pixel_a, ok ? "PASS" : "FAIL");
done:
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (readback) ID3D12Resource_Release(readback);
    if (single) ID3D12Resource_Release(single);
    if (msaa) ID3D12Resource_Release(msaa);
    return ok;
}

static int depth_case(Runtime *runtime, DXGI_FORMAT depth_format)
{
    D3D12_RESOURCE_DESC color_desc, depth_desc, readback_desc;
    D3D12_HEAP_PROPERTIES default_heap, readback_heap;
    D3D12_CLEAR_VALUE color_clear, depth_clear;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc, dsv_desc;
    ID3D12Resource *color = NULL, *depth = NULL, *color_readback = NULL, *depth_readback = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL, *dsv_heap = NULL;
    ID3D12PipelineState *pso = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, dsv;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT num_rows, descriptor_size;
    UINT64 row_size, total_size;
    D3D12_RESOURCE_BARRIER barriers[2];
    D3D12_TEXTURE_COPY_LOCATION src, dst;
    D3D12_VIEWPORT viewport = {0,0,FORMAT_WIDTH,FORMAT_HEIGHT,0,1};
    D3D12_RECT scissor = {0,0,FORMAT_WIDTH,FORMAT_HEIGHT};
    float clear_color[4] = {FORMAT_CLEAR_R, FORMAT_CLEAR_G, FORMAT_CLEAR_B, 1};
    HRESULT hr;
    int ok = 0;
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support;
        memset(&support, 0, sizeof(support)); support.Format = depth_format;
        hr = runtime->device->lpVtbl->CheckFeatureSupport(runtime->device, D3D12_FEATURE_FORMAT_SUPPORT,
                &support, sizeof(support));
        printf("  depth format support %-20s: hr=%s support1=0x%08x %s\n",
                depth_format == DXGI_FORMAT_D24_UNORM_S8_UINT ? "D24_UNORM_S8_UINT" : "D32_FLOAT_S8X24_UINT",
                hr_text(hr), support.Support1, SUCCEEDED(hr) ? "SUPPORTED" : "UNSUPPORTED");
        if (FAILED(hr)) return format_unsupported(hr) ? 1 : 0;
    }
    printf("  depth/stencil %-24s: begin\n", depth_format == DXGI_FORMAT_D24_UNORM_S8_UINT ? "D24_UNORM_S8_UINT" : "D32_FLOAT_S8X24_UINT");
    memset(&default_heap, 0, sizeof(default_heap)); default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(&readback_heap, 0, sizeof(readback_heap)); readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    memset(&color_desc, 0, sizeof(color_desc)); color_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    color_desc.Width=FORMAT_WIDTH; color_desc.Height=FORMAT_HEIGHT; color_desc.DepthOrArraySize=1;
    color_desc.MipLevels=1; color_desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; color_desc.SampleDesc.Count=1;
    color_desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN; color_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    memset(&color_clear, 0, sizeof(color_clear)); color_clear.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    memcpy(color_clear.Color, clear_color, sizeof(clear_color));
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &default_heap,
            D3D12_HEAP_FLAG_NONE, &color_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &color_clear, &IID_ID3D12Resource, (void **)&color);
    if (FAILED(hr)) goto done;
    memset(&depth_desc, 0, sizeof(depth_desc)); depth_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depth_desc.Width=FORMAT_WIDTH; depth_desc.Height=FORMAT_HEIGHT; depth_desc.DepthOrArraySize=1;
    depth_desc.MipLevels=1; depth_desc.Format=depth_format; depth_desc.SampleDesc.Count=1;
    depth_desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN; depth_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    memset(&depth_clear, 0, sizeof(depth_clear)); depth_clear.Format=depth_format;
    depth_clear.DepthStencil.Depth=1.0f; depth_clear.DepthStencil.Stencil=0;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &default_heap,
            D3D12_HEAP_FLAG_NONE, &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depth_clear, &IID_ID3D12Resource, (void **)&depth);
    if (FAILED(hr)) goto done;
    memset(&rtv_desc, 0, sizeof(rtv_desc)); rtv_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtv_desc.NumDescriptors=1;
    hr = runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device, &rtv_desc,
            &IID_ID3D12DescriptorHeap, (void **)&rtv_heap); if (FAILED(hr)) goto done;
    memset(&dsv_desc, 0, sizeof(dsv_desc)); dsv_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV; dsv_desc.NumDescriptors=1;
    hr = runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device, &dsv_desc,
            &IID_ID3D12DescriptorHeap, (void **)&dsv_heap); if (FAILED(hr)) goto done;
    rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
    dsv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dsv_heap, &dsv);
    runtime->device->lpVtbl->CreateRenderTargetView(runtime->device, color, NULL, rtv);
    { D3D12_DEPTH_STENCIL_VIEW_DESC view; memset(&view,0,sizeof(view)); view.Format=depth_format; view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
      runtime->device->lpVtbl->CreateDepthStencilView(runtime->device, depth, &view, dsv); }
    if (!create_format_pso(runtime, DXGI_FORMAT_B8G8R8A8_UNORM, depth_format, &pso)) goto done;
    memset(&readback_desc, 0, sizeof(readback_desc)); readback_desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width=FORMAT_READBACK_SIZE; readback_desc.Height=1; readback_desc.DepthOrArraySize=1;
    readback_desc.MipLevels=1; readback_desc.SampleDesc.Count=1; readback_desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &readback_heap,
            D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&color_readback); if (FAILED(hr)) goto done;
    runtime->device->lpVtbl->GetCopyableFootprints(runtime->device, &depth_desc, 0, 1, 0,
            &layout, &num_rows, &row_size, &total_size);
    readback_desc.Width=total_size;
    hr = runtime->device->lpVtbl->CreateCommittedResource(runtime->device, &readback_heap,
            D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&depth_readback); if (FAILED(hr)) goto done;
    runtime->allocator->lpVtbl->Reset(runtime->allocator); runtime->list->lpVtbl->Reset(runtime->list, runtime->allocator, pso);
    runtime->list->lpVtbl->OMSetRenderTargets(runtime->list, 1, &rtv, FALSE, &dsv);
    runtime->list->lpVtbl->ClearRenderTargetView(runtime->list, rtv, clear_color, 0, NULL);
    runtime->list->lpVtbl->ClearDepthStencilView(runtime->list, dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 7, 0, NULL);
    runtime->list->lpVtbl->OMSetStencilRef(runtime->list, 7);
    runtime->list->lpVtbl->RSSetViewports(runtime->list, 1, &viewport); runtime->list->lpVtbl->RSSetScissorRects(runtime->list, 1, &scissor);
    runtime->list->lpVtbl->IASetPrimitiveTopology(runtime->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); runtime->list->lpVtbl->DrawInstanced(runtime->list,3,1,0,0);
    memset(barriers,0,sizeof(barriers)); barriers[0].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barriers[0].Transition.pResource=color;
    barriers[0].Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET; barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE; barriers[0].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1]=barriers[0]; barriers[1].Transition.pResource=depth; barriers[1].Transition.StateBefore=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    runtime->list->lpVtbl->ResourceBarrier(runtime->list,2,barriers);
    memset(&dst,0,sizeof(dst)); dst.pResource=color_readback; dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format=DXGI_FORMAT_B8G8R8A8_UNORM; dst.PlacedFootprint.Footprint.Width=FORMAT_WIDTH; dst.PlacedFootprint.Footprint.Height=FORMAT_HEIGHT; dst.PlacedFootprint.Footprint.Depth=1; dst.PlacedFootprint.Footprint.RowPitch=FORMAT_ROW_PITCH;
    memset(&src,0,sizeof(src)); src.pResource=color; src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; runtime->list->lpVtbl->CopyTextureRegion(runtime->list,&dst,0,0,0,&src,NULL);
    dst.pResource=depth_readback; dst.PlacedFootprint=layout; src.pResource=depth; src.SubresourceIndex=0; runtime->list->lpVtbl->CopyTextureRegion(runtime->list,&dst,0,0,0,&src,NULL);
    if (FAILED(runtime->list->lpVtbl->Close(runtime->list))) goto done;
    { ID3D12CommandList *lists[]={(ID3D12CommandList *)runtime->list}; runtime->queue->lpVtbl->ExecuteCommandLists(runtime->queue,1,lists); }
    if (!signal_and_wait(runtime)) goto done;
    { D3D12_RANGE range={0,total_size}; unsigned char *p=NULL; if (FAILED(depth_readback->lpVtbl->Map(depth_readback,0,&range,(void **)&p)) || !p) goto done;
      float depth_value;
      if (depth_format == DXGI_FORMAT_D24_UNORM_S8_UINT) {
          UINT32 packed = (UINT32)p[0] | ((UINT32)p[1] << 8) |
                  ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
          depth_value = (float)(packed & 0x3fffffff) / (float)0x3fffffff;
      } else {
          depth_value = *(float *)p;
      }
      /* Native D3D12 exposes depth/stencil as one subresource here, while
       * this backend's copy footprint exposes only the depth plane. Stencil
       * clear/DSV behavior is exercised, but its readback plane is reported
       * unsupported rather than synthesized. */
      printf("  depth/stencil %-24s: footprint=%u rows=%u depth=%.3f stencil=UNSUPPORTED %s\n",
              depth_format == DXGI_FORMAT_D24_UNORM_S8_UINT ? "D24_UNORM_S8_UINT" : "D32_FLOAT_S8X24_UINT",
              layout.Footprint.RowPitch, num_rows, depth_value,
              depth_value > 0.9f ? "PASS (stencil clear/DSV exercised; readback unsupported)" : "FAIL");
      ok=depth_value > 0.9f; depth_readback->lpVtbl->Unmap(depth_readback,0,NULL); }
done:
    if (pso) ID3D12PipelineState_Release(pso); if (dsv_heap) ID3D12DescriptorHeap_Release(dsv_heap); if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (color_readback) ID3D12Resource_Release(color_readback); if (depth_readback) ID3D12Resource_Release(depth_readback); if (color) ID3D12Resource_Release(color); if (depth) ID3D12Resource_Release(depth);
    printf("  depth/stencil resource/DSV/barrier: %s\n", ok ? "PASS" : "FAIL"); return ok;
}

static int negative_format_tests(Runtime *runtime)
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    IDXGISwapChain1 *swapchain = NULL;
    IDXGISwapChain4 *swapchain4 = NULL;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_HEAP_PROPERTIES heap;
    ID3D12Resource *resource = NULL;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    D3D12_RENDER_TARGET_VIEW_DESC invalid_rtv;
    D3D12_DEPTH_STENCIL_VIEW_DESC invalid_dsv;
    HRESULT hr;
    int failures=0;
    printf("\n=== deterministic format/color negatives ===\n");
    memset(&desc,0,sizeof(desc)); desc.Width=FORMAT_WIDTH; desc.Height=FORMAT_HEIGHT; desc.Format=DXGI_FORMAT_BC1_UNORM; desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    hr=runtime->factory2->lpVtbl->CreateSwapChainForHwnd(runtime->factory2,(IUnknown *)runtime->queue,runtime->window,&desc,NULL,NULL,&swapchain);
    printf("  unsupported BC1 swapchain: hr=%s %s\n",hr_text(hr),FAILED(hr)&&format_unsupported(hr)?"PASS":"FAIL"); if (!(FAILED(hr)&&format_unsupported(hr))) failures++; if(swapchain) IDXGISwapChain1_Release(swapchain);
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.AlphaMode=(DXGI_ALPHA_MODE)99;
    hr=runtime->factory2->lpVtbl->CreateSwapChainForHwnd(runtime->factory2,(IUnknown *)runtime->queue,runtime->window,&desc,NULL,NULL,&swapchain);
    printf("  invalid alpha-mode swapchain: hr=%s %s\n",hr_text(hr),FAILED(hr)&&format_unsupported(hr)?"PASS":"FAIL"); if (!(FAILED(hr)&&format_unsupported(hr))) failures++; if(swapchain) IDXGISwapChain1_Release(swapchain);
    desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    hr=runtime->factory2->lpVtbl->CreateSwapChainForHwnd(runtime->factory2,(IUnknown *)runtime->queue,runtime->window,&desc,NULL,NULL,&swapchain);
    if (SUCCEEDED(hr)) {
        swapchain->lpVtbl->QueryInterface(swapchain,&IID_IDXGISwapChain4,(void **)&swapchain4);
        hr=swapchain4 ? swapchain4->lpVtbl->SetColorSpace1(swapchain4,DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) : E_NOINTERFACE;
        printf("  incompatible SDR-format/HDR10 color-space: hr=%s %s\n",hr_text(hr),FAILED(hr)?"PASS":"FAIL"); if(!FAILED(hr)) failures++;
    } else {
        printf("  incompatible SDR-format/HDR10 color-space: create hr=%s UNSUPPORTED\n",hr_text(hr));
    }
    if(swapchain4) IDXGISwapChain4_Release(swapchain4); if(swapchain) IDXGISwapChain1_Release(swapchain); swapchain4=NULL; swapchain=NULL;
    memset(&heap,0,sizeof(heap)); heap.Type=D3D12_HEAP_TYPE_DEFAULT; memset(&resource_desc,0,sizeof(resource_desc)); resource_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; resource_desc.Width=FORMAT_WIDTH; resource_desc.Height=FORMAT_HEIGHT; resource_desc.DepthOrArraySize=1; resource_desc.MipLevels=1; resource_desc.Format=DXGI_FORMAT_BC1_UNORM; resource_desc.SampleDesc.Count=1; resource_desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN; resource_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr=runtime->device->lpVtbl->CreateCommittedResource(runtime->device,&heap,D3D12_HEAP_FLAG_NONE,&resource_desc,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void **)&resource); printf("  unsupported BC1 render resource: hr=%s %s\n",hr_text(hr),FAILED(hr)&&format_unsupported(hr)?"PASS":"FAIL"); if(!(FAILED(hr)&&format_unsupported(hr))) failures++; if(resource) ID3D12Resource_Release(resource);
    memset(&heap_desc,0,sizeof(heap_desc)); heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap_desc.NumDescriptors=1; runtime->device->lpVtbl->CreateDescriptorHeap(runtime->device,&heap_desc,&IID_ID3D12DescriptorHeap,(void **)&rtv_heap); rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap,&handle);
    memset(&invalid_rtv,0,sizeof(invalid_rtv)); invalid_rtv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; invalid_rtv.ViewDimension=(D3D12_RTV_DIMENSION)99; runtime->device->lpVtbl->CreateRenderTargetView(runtime->device,NULL,&invalid_rtv,handle); printf("  invalid RTV description: PASS (void API returned)\n");
    memset(&invalid_dsv,0,sizeof(invalid_dsv)); invalid_dsv.Format=DXGI_FORMAT_B8G8R8A8_UNORM; invalid_dsv.ViewDimension=(D3D12_DSV_DIMENSION)99; runtime->device->lpVtbl->CreateDepthStencilView(runtime->device,NULL,&invalid_dsv,handle); printf("  invalid DSV description: PASS (void API returned)\n");
    if(rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    memset(&support,0,sizeof(support)); support.Format=(DXGI_FORMAT)0x7fffffffu; hr=runtime->device->lpVtbl->CheckFeatureSupport(runtime->device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)); printf("  invalid format support query: hr=%s support1=0x%08x support2=0x%08x %s\n",hr_text(hr),support.Support1,support.Support2,FAILED(hr)||(!support.Support1&&!support.Support2)?"PASS":"FAIL"); if(!FAILED(hr)&&(support.Support1||support.Support2)) failures++;
    printf("  negative result: %s (%d failures)\n",failures?"FAIL":"PASS",failures); return failures;
}

static int run_format_matrix(Runtime *runtime)
{
    UINT i;
    int failures=0;
    printf("\n=== format support and swapchain matrix ===\n");
    for(i=0;i<sizeof(format_specs)/sizeof(format_specs[0]);i++) {
        FormatCase fc; int query_ok=query_format(runtime,&format_specs[i]); int result;
        result=create_format_swapchain(runtime,&fc,&format_specs[i]);
        if(result==2) {
            printf("  swapchain format %s: UNSUPPORTED (HRESULT classified); testing offscreen path\n", format_specs[i].name);
            result = create_offscreen_format(runtime, &fc, &format_specs[i]);
            if (result == 2) { printf("  offscreen format %s: UNSUPPORTED (HRESULT classified)\n", format_specs[i].name); continue; }
            if (result == 3) continue;
        }
        if(!result) { failures++; continue; }
        if(!render_format_case(runtime,&fc,&format_specs[i])) failures++;
        format_release_case(&fc);
        if(!query_ok) failures++;
    }
    return failures;
}

int main(void)
{
    Runtime runtime;
    int failures=0;
    setvbuf(stdout,NULL,_IONBF,0);
    printf("=== DXGI-4 format/color/HDR probe ===\n");
    printf("real window, pinned adapter, format matrix, color policy, HDR classification, and depth/stencil\n");
    if(!create_runtime(&runtime)) { release_runtime(&runtime); return 1; }
    failures += run_format_matrix(&runtime);
    failures += resolve_case(&runtime) ? 0 : 1;
    failures += depth_case(&runtime, DXGI_FORMAT_D24_UNORM_S8_UINT) ? 0 : 1;
    failures += depth_case(&runtime, DXGI_FORMAT_D32_FLOAT_S8X24_UINT) ? 0 : 1;
    failures += color_space_matrix(&runtime);
    failures += negative_format_tests(&runtime);
    printf("\n=== DXGI-4 policy ===\n");
    printf("  tearing support: reported=%u; accepted presentation flag policy=%s\n",
            runtime.tearing_supported, runtime.tearing_supported ? "ALLOW_TEARING" : "NONE");
    release_runtime(&runtime);
    printf("DXGI-4 result: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
