// Find tile boundaries in sorted tile keys array
// After radix sort, keys are ordered by (TileID << 16 | Depth16).
// This shader identifies the start/end index range for each tile.

#include "compute_raster_types.h"

// Sorted pairs from radix sort: .x = tile key, .y = tile value index
[[vk::binding(0, 0)]] StructuredBuffer<uint2> sortedTilePairs;
[[vk::binding(1, 0)]] RWStructuredBuffer<int2> tileRanges;

[[vk::push_constant]] RangesPC pc;

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;

    if (idx >= pc.numTileInstances)
    {
        return;
    }

    uint currKey    = sortedTilePairs[idx].x;
    uint currTileID = UnpackTileID(currKey);

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
        uint prevKey    = sortedTilePairs[idx - 1].x;
        uint prevTileID = UnpackTileID(prevKey);

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
    if (idx == pc.numTileInstances - 1)
    {
        tileRanges[currTileID].y = int(idx + 1);
    }
}
