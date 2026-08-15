struct Payload { float4 a; uint b; uint pad[3]; };
RWStructuredBuffer<Payload> buf : register(u0);
[numthreads(8,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    Payload p;
    p.a = float4(float(tid.x), float(tid.y), float(tid.z), 7.0);
    p.b = tid.x * 2u + 1u;
    p.pad[0] = p.pad[1] = p.pad[2] = 0xDEADBEEFu;
    buf[tid.x] = p;
}
