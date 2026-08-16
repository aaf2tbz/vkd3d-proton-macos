/* DXGI-1 adapter identity probe for the DXVK-macOS + vkd3d-proton lane. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define COBJMACROS
#include <initguid.h>
#include <dxgi1_6.h>
#include <d3d12.h>

static const char *hr_text(HRESULT hr)
{
    static char text[32];
    snprintf(text, sizeof(text), "0x%08lx", (unsigned long)hr);
    return text;
}

static void print_luid(const char *label, LUID luid)
{
    printf("  %-18s: %08lx-%08lx\n", label,
            (unsigned long)luid.HighPart, (unsigned long)luid.LowPart);
}

static int same_luid(LUID a, LUID b)
{
    return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
}

static void print_module(const char *name)
{
    HMODULE module = GetModuleHandleA(name);
    char path[MAX_PATH];

    if (!module) {
        printf("  module %-12s: NOT LOADED\n", name);
        return;
    }
    if (!GetModuleFileNameA(module, path, sizeof(path)))
        strcpy(path, "<path unavailable>");
    printf("  module %-12s: %s\n", name, path);
}

static int factory_probe(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIFactory1 *factory1 = NULL;
    IDXGIFactory2 *factory2 = NULL;
    HRESULT hr;
    int failures = 0;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    printf("  CreateDXGIFactory : %s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (SUCCEEDED(hr)) IDXGIFactory_Release(factory);
    else failures++;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory1);
    printf("  CreateDXGIFactory1: %s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (SUCCEEDED(hr)) IDXGIFactory1_Release(factory1);
    else failures++;

    hr = CreateDXGIFactory2(0, &IID_IDXGIFactory2, (void **)&factory2);
    printf("  CreateDXGIFactory2: %s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : "UNAVAILABLE");
    if (SUCCEEDED(hr)) IDXGIFactory2_Release(factory2);

    return failures;
}

static int negative_probe(void)
{
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    ID3D12Device *device = NULL;
    HRESULT hr;
    int failures = 0;

    printf("=== DXGI-1 negative tests ===\n");
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr)) {
        printf("  factory setup: FAIL (%s)\n", hr_text(hr));
        return 1;
    }

    hr = IDXGIFactory1_EnumAdapters1(factory, 0xffffffffu, &adapter);
    printf("  invalid adapter index: %s %s\n", hr_text(hr),
            hr == DXGI_ERROR_NOT_FOUND ? "PASS" : "FAIL");
    if (hr != DXGI_ERROR_NOT_FOUND) failures++;
    if (adapter) IDXGIAdapter1_Release(adapter);

    hr = D3D12CreateDevice(NULL, (D3D_FEATURE_LEVEL)0xdead,
            &IID_ID3D12Device, (void **)&device);
    printf("  invalid D3D12 feature level: %s %s\n", hr_text(hr),
            hr == E_INVALIDARG ? "PASS" : "FAIL");
    if (hr != E_INVALIDARG) failures++;
    if (device) ID3D12Device_Release(device);

    IDXGIFactory1_Release(factory);
    printf("DXGI-1 negative result: %s (%d failure%s)\n",
            failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *selected = NULL;
    ID3D12Device *device = NULL;
    DXGI_ADAPTER_DESC1 selected_desc;
    LUID device_luid;
    HRESULT hr;
    UINT i, output_count = 0;
    int failures = 0;

    {
        char negative[8];
        BOOL negative_env = GetEnvironmentVariableA("DXGI_PROBE_NEGATIVE",
                negative, sizeof(negative)) != 0;
        if ((argc > 1 && !strcmp(argv[1], "--negative")) || negative_env)
            return negative_probe();
    }

    printf("=== DXGI-1 module identity ===\n");
    print_module("dxgi.dll");
    print_module("d3d12.dll");
    print_module("d3d12core.dll");

    printf("\n=== DXGI factory creation ===\n");
    failures += factory_probe();

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr)) {
        printf("\nAdapter enumeration: FAIL (%s)\n", hr_text(hr));
        return 1;
    }

    printf("\n=== adapter enumeration ===\n");
    for (i = 0; ; i++) {
        IDXGIAdapter1 *adapter = NULL;
        DXGI_ADAPTER_DESC1 desc;
        UINT j;
        HRESULT enum_hr = IDXGIFactory1_EnumAdapters1(factory, i, &adapter);

        if (enum_hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(enum_hr)) {
            printf("  adapter %u: enumeration failed (%s)\n", i, hr_text(enum_hr));
            failures++;
            break;
        }

        memset(&desc, 0, sizeof(desc));
        hr = IDXGIAdapter1_GetDesc1(adapter, &desc);
        printf("  adapter %u: desc=%s vendor=0x%04x device=0x%04x flags=0x%lx\n",
                i, hr_text(hr), desc.VendorId, desc.DeviceId,
                (unsigned long)desc.Flags);
        if (SUCCEEDED(hr)) {
            printf("    name: %ls\n", desc.Description);
            print_luid("adapter LUID", desc.AdapterLuid);
            printf("    dedicated VRAM: %llu MB\n",
                    (unsigned long long)(desc.DedicatedVideoMemory / (1024 * 1024)));
        }

        for (j = 0; ; j++) {
            IDXGIOutput *output = NULL;
            HRESULT output_hr = IDXGIAdapter1_EnumOutputs(adapter, j, &output);
            if (output_hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(output_hr)) {
                printf("    output %u: enumeration failed (%s)\n", j, hr_text(output_hr));
                break;
            }
            output_count++;
            IDXGIOutput_Release(output);
        }
        printf("    outputs: %u\n", j);

        if (!selected && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(hr)) {
            selected = adapter;
            selected_desc = desc;
            continue;
        }
        IDXGIAdapter1_Release(adapter);
    }
    printf("  total outputs: %u\n", output_count);

    if (!selected) {
        printf("  selected hardware adapter: NONE\n");
        IDXGIFactory1_Release(factory);
        return 1;
    }
    printf("  selected hardware adapter: PASS\n");
    printf("    name: %ls\n", selected_desc.Description);
    print_luid("selected LUID", selected_desc.AdapterLuid);

    printf("\n=== D3D12 adapter identity ===\n");
    hr = D3D12CreateDevice((IUnknown *)selected, D3D_FEATURE_LEVEL_11_0,
            &IID_ID3D12Device, (void **)&device);
    printf("  D3D12CreateDevice(adapter): %s %s\n", hr_text(hr),
            SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (FAILED(hr)) {
        failures++;
    } else {
        memset(&device_luid, 0, sizeof(device_luid));
        device->lpVtbl->GetAdapterLuid(device, &device_luid);
        print_luid("D3D12 LUID", device_luid);
        printf("  DXGI/D3D12 LUID match: %s\n",
                same_luid(selected_desc.AdapterLuid, device_luid) ? "PASS" : "FAIL");
        if (!same_luid(selected_desc.AdapterLuid, device_luid)) failures++;
        ID3D12Device_Release(device);
    }

    printf("\n=== d3d12core loadability ===\n");
    {
        HMODULE core = LoadLibraryA("d3d12core.dll");
        FARPROC get_interface = core ? GetProcAddress(core, "D3D12GetInterface") : NULL;
        FARPROC sdk_version = core ? GetProcAddress(core, "D3D12SDKVersion") : NULL;
        print_module("d3d12core.dll");
        printf("  LoadLibrary(d3d12core): %s\n", core ? "PASS" : "FAIL");
        printf("  D3D12GetInterface export: %s\n", get_interface ? "PASS" : "FAIL");
        printf("  D3D12SDKVersion export: %s\n", sdk_version ? "PASS" : "FAIL");
        if (!core || !get_interface || !sdk_version) failures++;
        if (core) FreeLibrary(core);
    }

    IDXGIAdapter1_Release(selected);
    IDXGIFactory1_Release(factory);
    printf("\nDXGI-1 result: %s (%d failure%s)\n",
            failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
