RWStructuredBuffer<uint> buf : register(u0);
[numthreads(8,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    // SM 6.0 wave ops: lane count + 32-bit prefix sum
    uint wave = WaveGetLaneCount();
    uint sum = WavePrefixSum(1u); // 32-bit - Metal simd_prefix_exclusive_sum supports 32-bit
    buf[tid.x] = wave + sum;
}
