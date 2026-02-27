[[vk::binding(0, 0)]] RWStructuredBuffer<uint2> sortedPairs;
[[vk::binding(1, 0)]] StructuredBuffer<uint> tileTileIDs;

// Indirect args buffer to read actual element count
[[vk::binding(2, 0)]] ByteAddressBuffer sortParams;

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;

    uint numElements = sortParams.Load(0);

    if (idx >= numElements)
    {
        return;
    }

    uint2 pair = sortedPairs[idx];
    // pair.y is the original tile instance index (self-index from preprocess)
    // Look up the tileID for this tile instance
    pair.x = tileTileIDs[pair.y];
    sortedPairs[idx] = pair;
}
