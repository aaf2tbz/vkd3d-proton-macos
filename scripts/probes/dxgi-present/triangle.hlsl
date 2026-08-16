struct VSOut {
    float4 position : SV_Position;
};

VSOut vs(uint vertex_id : SV_VertexID)
{
    float2 positions[3] = {
        float2(-0.75, -0.75),
        float2( 0.75, -0.75),
        float2( 0.00,  0.75)
    };
    VSOut output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    return output;
}

float4 ps() : SV_Target
{
    return float4(1.0, 0.125, 0.0, 1.0);
}
