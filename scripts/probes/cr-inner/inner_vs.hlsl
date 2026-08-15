struct VSIn {
    float2 pos : POSITION;
};
struct VSOut {
    float4 pos : SV_Position;
};
VSOut main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 0, 1);
    return o;
}
