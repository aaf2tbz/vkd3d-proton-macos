RWStructuredBuffer<uint> buf : register(u0);
groupshared uint gs[8];
[numthreads(8,1,1)]
void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID) {
    gs[gi] = tid.x * 3u + 1u;
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0u) {
        uint s = 0;
        for (uint i = 0; i < 8; i++) s += gs[i];
        buf[gid.x] = s; // sum of 3*tid+1 over the group
    }
}
