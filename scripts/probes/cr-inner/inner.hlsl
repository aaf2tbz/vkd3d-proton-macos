struct PSIn {
    float4 pos : SV_Position;
};
float4 main(PSIn i, bool ic : SV_InnerCoverage) : SV_Target {
    // diagnostics: R = ic, G = pos.x/64, B = pos.y/64
    return float4(ic ? 1.0 : 0.0, i.pos.x / 64.0, i.pos.y / 64.0, 1.0);
}
