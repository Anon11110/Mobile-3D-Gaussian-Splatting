// Radix Prefix Scan with Wave Intrinsics
// Requires reliable wave support (works on Nvidia, Samsung Xclipse, but not Qualcomm Adreno)

#define WORKGROUP_SIZE 256
#define SUBGROUP_SIZE 32
#define ELEMENTS_PER_THREAD 4

#ifdef INDIRECT_DISPATCH
[[vk::binding(0, 1)]] ByteAddressBuffer sortParams;
struct PushConstants { uint passType; };
#else
struct PushConstants
{
    uint numElements;
    uint passType;  // 0 = scan blocks, 1 = scan block sums, 2 = add offsets
};
#endif
[[vk::push_constant]] PushConstants pc;

#ifdef INDIRECT_DISPATCH
// passType 1 (scan block sums) operates on numScanWorkgroups elements;
// passType 0 (scan blocks) and 2 (add offsets) operate on numScanElements.
uint GetNumElements()
{
    return (pc.passType == 1) ? sortParams.Load(16) : sortParams.Load(12);
}
#else
uint GetNumElements() { return pc.numElements; }
#endif

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> data;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> blockSums;

groupshared uint subgroupSums[WORKGROUP_SIZE / SUBGROUP_SIZE];
groupshared uint subgroupPrefixes[WORKGROUP_SIZE / SUBGROUP_SIZE];
groupshared uint blockTotalSum;

// Generic block scanning function used by Pass 0 and 1
void ScanElements(uint lID, uint wID)
{
    const uint numElements = GetNumElements();
    uint waveId = lID / WaveGetLaneCount();
    uint numWaves = WORKGROUP_SIZE / WaveGetLaneCount();

    uint elementsPerWorkgroup = WORKGROUP_SIZE * ELEMENTS_PER_THREAD;
    uint blockStart = wID * elementsPerWorkgroup;

    uint values[ELEMENTS_PER_THREAD];
    uint threadSum = 0;

    [unroll]
    for (uint i = 0; i < ELEMENTS_PER_THREAD; ++i)
    {
        uint idx = blockStart + lID * ELEMENTS_PER_THREAD + i;
        values[i] = (idx < numElements) ? data[idx] : 0;
        threadSum += values[i];
    }

    // Wave-level exclusive scan
    uint wavePrefix = WavePrefixSum(threadSum);
    uint waveTotal = WaveActiveSum(threadSum);

    if (WaveIsFirstLane())
    {
        subgroupSums[waveId] = waveTotal;
    }
    GroupMemoryBarrierWithGroupSync();

    // Sequential scan of wave sums by thread 0
    if (lID == 0)
    {
        uint runningSum = 0;
        for (uint i = 0; i < numWaves; ++i)
        {
            subgroupPrefixes[i] = runningSum;
            runningSum += subgroupSums[i];
        }
        blockTotalSum = runningSum;
    }
    GroupMemoryBarrierWithGroupSync();

    // Compute and write final prefix for each element
    uint basePrefix = subgroupPrefixes[waveId] + wavePrefix;
    uint currentPrefix = basePrefix;

    [unroll]
    for (uint i = 0; i < ELEMENTS_PER_THREAD; ++i)
    {
        uint idx = blockStart + lID * ELEMENTS_PER_THREAD + i;
        if (idx < numElements)
        {
            data[idx] = currentPrefix;
            currentPrefix += values[i];
        }
    }

    // Pass 0 is the only pass that writes out the block sum
    if (lID == 0 && pc.passType == 0)
    {
        blockSums[wID] = blockTotalSum;
    }
}

// Pass 2 function to add the scanned block offsets
void AddBlockOffsets(uint lID, uint wID)
{
    const uint numElements = GetNumElements();
    uint elementsPerWorkgroup = WORKGROUP_SIZE * ELEMENTS_PER_THREAD;
    uint blockStart = wID * elementsPerWorkgroup;

    uint offset = blockSums[wID];

    for (uint i = lID; i < elementsPerWorkgroup; i += WORKGROUP_SIZE)
    {
        uint idx = blockStart + i;
        if (idx < numElements)
        {
            data[idx] += offset;
        }
    }
}

[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    uint lID = groupThreadId.x;
    uint wID = groupId.x;

    if (pc.passType == 0 || pc.passType == 1)
    {
        // Pass 0: Scans blocks of histogram data.
        // Pass 1: Reused to scan the block sums themselves.
        ScanElements(lID, wID);
    }
    else if (pc.passType == 2)
    {
        // Pass 2: Adds the scanned block offsets back to each block.
        AddBlockOffsets(lID, wID);
    }
}
