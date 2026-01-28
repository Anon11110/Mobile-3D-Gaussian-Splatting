// Radix Histogram with Wave-Private Histograms
// Requires reliable wave built-ins (works on Nvidia, Samsung Xclipse, but not Qualcomm Adreno)

#define WORKGROUP_SIZE 256
#define RADIX_SORT_BINS 256

struct PushConstants
{
    uint numElements;
    uint shift;
    uint numWorkgroups;
    uint numBlocksPerWorkgroup;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] StructuredBuffer<uint> depthKeys;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> histograms;

static const uint waveSize = 32;
static const uint maxWaves = WORKGROUP_SIZE / waveSize;

// Final per-WG histogram (256 bins)
groupshared uint ldsHist[RADIX_SORT_BINS];

// Wave-private histograms to reduce LDS hot spots
groupshared uint ldsWaveHist[RADIX_SORT_BINS * maxWaves];

[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    const uint lid = groupThreadId.x;
    const uint wid = groupId.x;
    const uint waveId = WaveGetLaneIndex() == 0 ? (lid / WaveGetLaneCount()) : (lid / WaveGetLaneCount());
    const uint numWaves = WORKGROUP_SIZE / WaveGetLaneCount();

    if (lid < RADIX_SORT_BINS)
    {
        ldsHist[lid] = 0u;
    }

    for (uint i = lid; i < RADIX_SORT_BINS * maxWaves; i += WORKGROUP_SIZE)
    {
        ldsWaveHist[i] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint wgSpan = pc.numBlocksPerWorkgroup * WORKGROUP_SIZE;
    const uint baseElem = wid * wgSpan + lid;
    const uint mask = RADIX_SORT_BINS - 1u;

    // Accumulate into wave-private histogram to reduce contention
    for (uint block = 0u; block < pc.numBlocksPerWorkgroup; ++block)
    {
        const uint elemId = baseElem + block * WORKGROUP_SIZE;
        if (elemId < pc.numElements)
        {
            const uint key = depthKeys[elemId];
            const uint bin = (key >> pc.shift) & mask;
            uint original;
            InterlockedAdd(ldsWaveHist[bin * maxWaves + waveId], 1u, original);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (lid < RADIX_SORT_BINS)
    {
        uint sum = 0u;
        for (uint w = 0u; w < numWaves; ++w)
        {
            sum += ldsWaveHist[lid * maxWaves + w];
        }
        ldsHist[lid] = sum;
    }
    GroupMemoryBarrierWithGroupSync();

    if (lid < RADIX_SORT_BINS)
    {
        // Write in bin-major order: [bin0: WG0..WGn | bin1: WG0..WGn | ... | bin255: WG0..WGn]
        histograms[lid * pc.numWorkgroups + wid] = ldsHist[lid];
    }
}
