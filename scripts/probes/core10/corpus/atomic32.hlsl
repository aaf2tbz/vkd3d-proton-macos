RWStructuredBuffer<uint> buf : register(u0);
[numthreads(8,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint old;
    InterlockedAdd(buf[0], 1u, old);
    InterlockedMax(buf[1], tid.x, old);
    buf[2] = old; // last-add old value (unsynchronized, but >= 0)
}
