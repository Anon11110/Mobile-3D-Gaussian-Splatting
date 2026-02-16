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

// Input: interleaved uint2 pairs (.x = depth key, .y = splat index)
[[vk::binding(0, 0)]] StructuredBuffer<uint2> inputPairs;

// Output: unpacked splat indices only (final pass writes indices directly)
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> outputIndices;

// Exclusively scanned global offsets from scan passes
[[vk::binding(2, 0)]] StructuredBuffer<uint> scannedGlobalOffsets;

groupshared uint localOffsets[RADIX_SORT_BINS];

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

    if (lID < RADIX_SORT_BINS)
    {
        // Histogram: [bin0: WG0..WGn | bin1: WG0..WGn | ...]
        localOffsets[lID] = scannedGlobalOffsets[lID * pc.numWorkgroups + wID];
    }
    GroupMemoryBarrierWithGroupSync();

    // Scatter indices according to global offsets
    const uint flagsBin = lID / 32;
    const uint flagsBit = 1u << (lID % 32);

    for (uint index = 0; index < pc.numBlocksPerWorkgroup; index++)
    {
        uint elementId = wID * pc.numBlocksPerWorkgroup * WORKGROUP_SIZE + index * WORKGROUP_SIZE + lID;

        if (lID < RADIX_SORT_BINS)
        {
            [unroll]
            for (int i = 0; i < WORKGROUP_SIZE / 32; i++)
            {
                binFlags[lID].flags[i] = 0U;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        uint2 pair = uint2(0, 0);
        uint binID = 0;

        if (elementId < pc.numElements)
        {
            pair = inputPairs[elementId];
            binID = uint(pair.x >> pc.shift) & uint(RADIX_SORT_BINS - 1);
            uint original;
            InterlockedOr(binFlags[binID].flags[flagsBin], flagsBit, original);
        }
        GroupMemoryBarrierWithGroupSync();

        if (elementId < pc.numElements)
        {
            uint binOffset = localOffsets[binID];

            // Calculate output index of element
            uint prefix = 0;
            for (uint i = 0; i < WORKGROUP_SIZE / 32; i++)
            {
                const uint bits = binFlags[binID].flags[i];
                const uint fullCount = countbits(bits);
                const uint partialCount = countbits(bits & (flagsBit - 1));
                prefix += (i < flagsBin) ? fullCount : 0U;
                prefix += (i == flagsBin) ? partialCount : 0U;
            }

            uint outputIdx = binOffset + prefix;
            // Write only the splat index
            outputIndices[outputIdx] = pair.y;
        }
        GroupMemoryBarrierWithGroupSync();

        // Update local offsets for next block, all threads update their respective bin
        if (lID < RADIX_SORT_BINS)
        {
            uint count = 0;
            for (uint k = 0; k < WORKGROUP_SIZE / 32; k++)
            {
                count += countbits(binFlags[lID].flags[k]);
            }
            localOffsets[lID] += count;
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
