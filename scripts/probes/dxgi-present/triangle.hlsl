struct VSOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VSOut vs(uint vertex_id : SV_VertexID)
{
    float2 positions[3] = {
        float2(-0.75, -0.75),
        float2( 0.75, -0.75),
        float2( 0.00,  0.75)
    };
    float3 colors[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0)
    };
    VSOut output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.color = colors[vertex_id];
    return output;
}

float4 ps(VSOut input) : SV_Target
{
    return float4(input.color, 1.0);
}
