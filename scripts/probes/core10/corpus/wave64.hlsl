RWStructuredBuffer<uint> buf : register(u0);
[numthreads(64,1,1)]
void main(uint tid : SV_DispatchThreadID) {
    uint wave = WaveGetLaneCount();
    uint64_t big = WavePrefixSum(1ull) + 2ull;
    buf[tid] = uint(wave) + uint(big & 0xffffffffu);
}
