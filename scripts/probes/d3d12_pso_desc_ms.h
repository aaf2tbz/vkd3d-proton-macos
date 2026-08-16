/* The mingw d3d12.h predates the mesh PSO fields; the real D3D12 layout
   inserts MS (mesh) and AS (amplification) between GS and StreamOutput.
   Shims the app-side struct to the real ABI so the vkd3d (which reads the
   MS/AS bytecodes) sees the correct layout. */
typedef struct D3D12_GRAPHICS_PIPELINE_STATE_DESC_MS {
    ID3D12RootSignature *pRootSignature;
    D3D12_SHADER_BYTECODE VS;
    D3D12_SHADER_BYTECODE PS;
    D3D12_SHADER_BYTECODE DS;
    D3D12_SHADER_BYTECODE HS;
    D3D12_SHADER_BYTECODE GS;
    D3D12_SHADER_BYTECODE MS;
    D3D12_SHADER_BYTECODE AS;
    D3D12_STREAM_OUTPUT_DESC StreamOutput;
    D3D12_BLEND_DESC BlendState;
    UINT SampleMask;
    D3D12_RASTERIZER_DESC RasterizerState;
    D3D12_DEPTH_STENCIL_DESC DepthStencilState;
    D3D12_INPUT_LAYOUT_DESC InputLayout;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
    UINT NumRenderTargets;
    DXGI_FORMAT RTVFormats[8];
    DXGI_FORMAT DSVFormat;
    DXGI_SAMPLE_DESC SampleDesc;
    UINT NodeMask;
    D3D12_CACHED_PIPELINE_STATE CachedPSO;
    D3D12_PIPELINE_STATE_FLAGS Flags;
} D3D12_GRAPHICS_PIPELINE_STATE_DESC_MS;
