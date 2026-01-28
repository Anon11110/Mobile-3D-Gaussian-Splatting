#define WORKGROUP_SIZE 256
#define RADIX_SORT_BINS 256
#define SUBGROUP_SIZE 32

struct PushConstants
{
    uint numElements;
    uint shift;
    uint numWorkgroups;
    uint numBlocksPerWorkgroup;
};
[[vk::push_constant]] PushConstants pc;

// Input: Depth keys and splat indices from previous pass (or original)
[[vk::binding(0, 0)]] StructuredBuffer<uint> inputDepthKeys;
[[vk::binding(1, 0)]] StructuredBuffer<uint> inputSplatIndices;

// Output: Sorted depth keys and splat indices
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> outputDepthKeys;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> outputSplatIndices;

// Histograms from histogram pass
// Layout: [bin0: WG0..WGn | bin1: WG0..WGn | ... ]
[[vk::binding(4, 0)]] StructuredBuffer<uint> histograms;

groupshared uint sums[RADIX_SORT_BINS / SUBGROUP_SIZE];
groupshared uint globalOffsets[RADIX_SORT_BINS];

struct BinFlags
{
    uint flags[WORKGROUP_SIZE / 32];
};
groupshared BinFlags binFlags[RADIX_SORT_BINS];

[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID,
          uint3 groupThreadId : SV_GroupThreadID,
          uint3 groupId : SV_GroupID)
{
    uint gID = dispatchThreadId.x;
    uint lID = groupThreadId.x;
    uint wID = groupId.x;
    uint waveId = lID / WaveGetLaneCount();
    uint laneId = WaveGetLaneIndex();

    uint localHistogram = 0;
    uint prefixSum = 0;
    uint histogramCount = 0;

    // Calculate global prefix sum for histogram bins
    if (lID < RADIX_SORT_BINS)
    {
        uint count = 0;
        for (uint j = 0; j < pc.numWorkgroups; j++)
        {
            // Histogram is stored in bin-major order: [bin0: WG0..WGn | bin1: WG0..WGn | ...]
            const uint t = histograms[lID * pc.numWorkgroups + j];
            localHistogram = (j == wID) ? count : localHistogram;
            count += t;
        }
        histogramCount = count;

        // Wave-level reduction and prefix sum
        const uint sum = WaveActiveSum(histogramCount);
        prefixSum = WavePrefixSum(histogramCount);

        if (WaveIsFirstLane())
        {
            sums[waveId] = sum;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // Complete the global prefix sum
    if (lID < RADIX_SORT_BINS)
    {
        uint sumsPrefixSum = 0;

        // First, have wave 0 scan the sums array
        if (waveId == 0)
        {
            uint mySum = 0;
            if (laneId < (RADIX_SORT_BINS / SUBGROUP_SIZE))
            {
                mySum = sums[laneId];
            }

            uint scannedSum = WavePrefixSum(mySum);

            if (laneId < (RADIX_SORT_BINS / SUBGROUP_SIZE))
            {
                sums[laneId] = scannedSum;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // Now read the scanned sum for our subgroup
        sumsPrefixSum = sums[waveId];
        const uint globalHistogram = sumsPrefixSum + prefixSum;
        globalOffsets[lID] = globalHistogram + localHistogram;
    }
    GroupMemoryBarrierWithGroupSync();

    // Scatter keys and indices according to global offsets
    const uint flagsBin = lID / 32;
    const uint flagsBit = 1u << (lID % 32);

    for (uint index = 0; index < pc.numBlocksPerWorkgroup; index++)
    {
        uint elementId = wID * pc.numBlocksPerWorkgroup * WORKGROUP_SIZE + index * WORKGROUP_SIZE + lID;

        // Initialize bin flags
        if (lID < RADIX_SORT_BINS)
        {
            [unroll]
            for (int i = 0; i < WORKGROUP_SIZE / 32; i++)
            {
                binFlags[lID].flags[i] = 0U;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        uint depthKey = 0;
        uint splatIdx = 0;
        uint binID = 0;
        uint binOffset = 0;

        if (elementId < pc.numElements)
        {
            depthKey = inputDepthKeys[elementId];
            splatIdx = inputSplatIndices[elementId];
            binID = uint(depthKey >> pc.shift) & uint(RADIX_SORT_BINS - 1);

            binOffset = globalOffsets[binID];

            uint original;
            InterlockedOr(binFlags[binID].flags[flagsBin], flagsBit, original);
        }
        GroupMemoryBarrierWithGroupSync();

        // Compute exclusive scan of the set flags (0..0 for thread 0, 0..1 for thread 1, 0..2 for thread 2, etc)
        uint bits = 0u;
        for (uint k = 0u; k < flagsBin; ++k)
        {
            bits += countbits(binFlags[binID].flags[k]);
        }

        uint myFlags = binFlags[binID].flags[flagsBin];
        uint ltMask = (1u << (lID % 32)) - 1u;
        bits += countbits(myFlags & ltMask);

        if (elementId < pc.numElements)
        {
            outputDepthKeys[binOffset + bits] = depthKey;
            outputSplatIndices[binOffset + bits] = splatIdx;
        }
        GroupMemoryBarrierWithGroupSync();

        // Update offsets for each bin
        if (lID < RADIX_SORT_BINS)
        {
            uint binBits = 0;
            for (uint k = 0; k < WORKGROUP_SIZE / 32; k++)
            {
                binBits += countbits(binFlags[lID].flags[k]);
            }
            globalOffsets[lID] += binBits;
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
