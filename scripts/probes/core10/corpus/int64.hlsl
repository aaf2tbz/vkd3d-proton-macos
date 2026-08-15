RWStructuredBuffer<uint> buf : register(u0);
[numthreads(8,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    // 64-bit multiply: tid * 0x100000001 = tid*2^32 + tid -> >>32 == tid
    uint64_t a = (uint64_t)tid.x * 0x100000001ull;
    // 64-bit add with carry
    uint64_t b = a + 0xFFFFFFFFull;
    buf[tid.x] = uint(b >> 32) + uint(b & 0xFFFFFFFFull) - tid.x * 2u + 1u;
}
