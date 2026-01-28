#define WORKGROUP_SIZE 256
#define ELEMENTS_PER_THREAD 4

struct PushConstants
{
    uint numElements;
    uint passType;  // 0 = scan blocks, 1 = scan block sums, 2 = add offsets
};
[[vk::push_constant]] PushConstants pc;

// Main data buffer for input/output.
// Pass 0(scan blocks): Reads from Histograms, writes intermediate scan to Histograms.
// Pass 1(scan block sums): Reads/writes BlockSums in-place.
// Pass 2(add offsets): Reads BlockSums, adds to Histograms in-place.
[[vk::binding(0, 0)]] RWStructuredBuffer<uint> data;

[[vk::binding(1, 0)]] RWStructuredBuffer<uint> blockSums;

groupshared uint threadSums[WORKGROUP_SIZE];
groupshared uint scanTemp[WORKGROUP_SIZE * 2];  // Double buffer for Blelloch scan
groupshared uint blockTotalSum;

void SharedMemoryExclusiveScan(uint lID)
{
    uint n = WORKGROUP_SIZE;

    scanTemp[lID] = threadSums[lID];
    GroupMemoryBarrierWithGroupSync();

    // Up-sweep phase
    uint offset = 1;
    for (uint d = n >> 1; d > 0; d >>= 1)
    {
        GroupMemoryBarrierWithGroupSync();
        if (lID < d)
        {
            uint ai = offset * (2 * lID + 1) - 1;
            uint bi = offset * (2 * lID + 2) - 1;
            scanTemp[bi] += scanTemp[ai];
        }
        offset *= 2;
    }

    if (lID == 0)
    {
        blockTotalSum = scanTemp[n - 1];
        scanTemp[n - 1] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // Down-sweep phase
    for (uint d = 1; d < n; d *= 2)
    {
        offset >>= 1;
        GroupMemoryBarrierWithGroupSync();
        if (lID < d)
        {
            uint ai = offset * (2 * lID + 1) - 1;
            uint bi = offset * (2 * lID + 2) - 1;
            uint temp = scanTemp[ai];
            scanTemp[ai] = scanTemp[bi];
            scanTemp[bi] += temp;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    threadSums[lID] = scanTemp[lID];
    GroupMemoryBarrierWithGroupSync();
}

// Generic block scanning function used by Pass 0 and 1
void ScanElements(uint lID, uint wID)
{
    uint elementsPerWorkgroup = WORKGROUP_SIZE * ELEMENTS_PER_THREAD;
    uint blockStart = wID * elementsPerWorkgroup;

    uint values[ELEMENTS_PER_THREAD];
    uint threadSum = 0;

    [unroll]
    for (uint i = 0; i < ELEMENTS_PER_THREAD; ++i)
    {
        uint idx = blockStart + lID * ELEMENTS_PER_THREAD + i;
        values[i] = (idx < pc.numElements) ? data[idx] : 0;
        threadSum += values[i];
    }

    threadSums[lID] = threadSum;
    GroupMemoryBarrierWithGroupSync();

    SharedMemoryExclusiveScan(lID);

    uint threadPrefix = threadSums[lID];
    uint currentPrefix = threadPrefix;

    [unroll]
    for (uint i = 0; i < ELEMENTS_PER_THREAD; ++i)
    {
        uint idx = blockStart + lID * ELEMENTS_PER_THREAD + i;
        if (idx < pc.numElements)
        {
            data[idx] = currentPrefix;
            currentPrefix += values[i];
        }
    }

    if (lID == 0 && pc.passType == 0)
    {
        blockSums[wID] = blockTotalSum;
    }
}

// Pass 2 function to add the scanned block offsets
void AddBlockOffsets(uint lID, uint wID)
{
    uint elementsPerWorkgroup = WORKGROUP_SIZE * ELEMENTS_PER_THREAD;
    uint blockStart = wID * elementsPerWorkgroup;

    uint offset = blockSums[wID];

    for (uint i = lID; i < elementsPerWorkgroup; i += WORKGROUP_SIZE)
    {
        uint idx = blockStart + i;
        if (idx < pc.numElements)
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
