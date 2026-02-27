// Find tile boundaries in sorted tile keys array
// After radix sort, keys are ordered by (TileID << 16 | Depth16).
// This shader identifies the start/end index range for each tile.

#include "compute_raster_types.h"

// Sorted pairs from radix sort: .x = tile key, .y = tile value index
[[vk::binding(0, 0)]] StructuredBuffer<uint2> sortedTilePairs;
[[vk::binding(1, 0)]] RWStructuredBuffer<int2> tileRanges;

// Indirect args buffer, the actual tile instance count
[[vk::binding(2, 0)]] ByteAddressBuffer sortParams;

[[vk::push_constant]] RangesPC pc;

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;

    // Read actual tile instance count from indirect sort params
    uint numTileInstances = sortParams.Load(0);

    if (idx >= numTileInstances)
    {
        return;
    }

    // After two pass sort, pair.x is the tileID directly
    uint currTileID = sortedTilePairs[idx].x;

    // Skip invalid entries (0xFFFFFFFF padding from unused buffer space)
    if (currTileID >= pc.numTiles)
    {
        return;
    }

    // Check if this is a tile boundary
    if (idx == 0)
    {
        // First entry: always starts a new tile
        tileRanges[currTileID].x = int(idx);
    }
    else
    {
        uint prevTileID = sortedTilePairs[idx - 1].x;

        if (currTileID != prevTileID)
        {
            // Start of a new tile
            tileRanges[currTileID].x = int(idx);

            // End of the previous tile
            if (prevTileID < pc.numTiles)
            {
                tileRanges[prevTileID].y = int(idx);
            }
        }
    }

    // Handle the last entry
    if (idx == numTileInstances - 1)
    {
        tileRanges[currTileID].y = int(idx + 1);
    }
}
