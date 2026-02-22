#define WORKGROUP_SIZE 256
#define RADIX_SORT_BINS 256

#ifdef INDIRECT_DISPATCH
[[vk::binding(0, 1)]] ByteAddressBuffer sortParams;
struct PushConstants { uint shift; };
#else
struct PushConstants
{
    uint numElements;
    uint shift;
    uint numWorkgroups;
    uint numBlocksPerWorkgroup;
};
#endif
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] StructuredBuffer<uint2> sortPairs;  // .x = depth key, .y = splat index
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> histograms;

// Final per-WG histogram (256 bins)
groupshared uint ldsHist[RADIX_SORT_BINS];

[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    const uint lid = groupThreadId.x;
    const uint wid = groupId.x;

#ifdef INDIRECT_DISPATCH
    const uint numElements          = sortParams.Load(0);
    const uint numWorkgroups        = sortParams.Load(4);
    const uint numBlocksPerWorkgroup = sortParams.Load(8);
#else
    const uint numElements          = pc.numElements;
    const uint numWorkgroups        = pc.numWorkgroups;
    const uint numBlocksPerWorkgroup = pc.numBlocksPerWorkgroup;
#endif

    if (lid < RADIX_SORT_BINS)
    {
        ldsHist[lid] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint wgSpan = numBlocksPerWorkgroup * WORKGROUP_SIZE;
    const uint baseElem = wid * wgSpan + lid;
    const uint mask = RADIX_SORT_BINS - 1u;

    // Accumulate histogram counts using atomics
    for (uint block = 0u; block < numBlocksPerWorkgroup; ++block)
    {
        const uint elemId = baseElem + block * WORKGROUP_SIZE;
        if (elemId < numElements)
        {
            const uint key = sortPairs[elemId].x;
            const uint bin = (key >> pc.shift) & mask;
            uint original;
            InterlockedAdd(ldsHist[bin], 1u, original);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (lid < RADIX_SORT_BINS)
    {
        // Write in bin-major order: [bin0: WG0..WGn | bin1: WG0..WGn | ... | bin255: WG0..WGn]
        histograms[lid * numWorkgroups + wid] = ldsHist[lid];
    }
}
