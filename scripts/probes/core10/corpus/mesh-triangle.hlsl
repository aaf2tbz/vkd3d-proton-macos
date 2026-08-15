struct MeshOut {
    float4 pos : SV_Position;
    float3 color : COLOR0;
};
[outputtopology("triangle")]
[numthreads(64,1,1)]
void main(uint gtid : SV_GroupThreadID,
          out vertices MeshOut v[64], out indices uint3 t[96]) {
    SetMeshOutputCounts(3, 1);
    v[0].pos = float4(0,0,0,1); v[0].color = float3(1,0,0);
    v[1].pos = float4(1,0,0,1); v[1].color = float3(0,1,0);
    v[2].pos = float4(0,1,0,1); v[2].color = float3(0,0,1);
    t[0] = uint3(0,1,2);
}
