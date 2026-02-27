// Shared data structures for tile-based compute rasterization pipeline

#ifndef COMPUTE_RASTER_TYPES_H
#define COMPUTE_RASTER_TYPES_H

// Gaussian2D: Pre-computed 2D projection data for rasterization
struct Gaussian2D
{
    float4 conicOpacity;    // Inverse 2D covariance (a, b, c) + opacity in w
                            // Quadratic form: a*x^2 + 2*b*x*y + c*y^2
    float4 color;           // Final RGB color (after SH evaluation) + alpha
    float2 screenPos;       // Screen-space center position (pixels)
    float  radius;          // Maximum bounding radius for tile coverage (pixels)
    float  depth;           // Linear view-space depth (for debugging/verification)
};
// Size: 48 bytes (12 floats), naturally aligned

// Preprocess push constants
struct PreprocessPC
{
    uint  numSplats;    // Total number of splats to process
    uint  tilesX;       // Number of tiles in X dimension
    uint  tilesY;       // Number of tiles in Y dimension
    uint  tileSize;     // Tile size in pixels (typically 16)
    float nearPlane;    // Near plane distance for depth normalization
    float farPlane;     // Far plane distance for depth normalization
    uint  maxTileInstances; // Maximum tile instances buffer capacity
    uint  _pad0;        // Padding to align to 16 bytes
};

// IdentifyRanges push constants
struct RangesPC
{
    uint numTileInstances;  // Total number of tile instances after preprocess
    uint numTiles;          // Total number of tiles (tilesX * tilesY)
};

// Rasterize push constants
struct RasterPC
{
    uint tilesX;        // Number of tiles in X dimension
    uint tilesY;        // Number of tiles in Y dimension
    uint screenWidth;   // Screen width in pixels
    uint screenHeight;  // Screen height in pixels
    uint transmittanceStatsMode; // 0=off, 1=stats only, 2=stats+heatmap
    uint _rasterPad0;   // Padding to 8-byte alignment
};

// Tile range structure: stores start and end indices for each tile
// in the sorted tile instance array
struct TileRange
{
    int start;  // First index (inclusive) in tileValues for this tile
    int end;    // Last index (exclusive) in tileValues for this tile
};

// Helper functions for 32-bit packed tile key format:
// Key = (TileID << 16) | Depth16
//
// TileID (upper 16 bits): 0-65,535 tiles
// Depth16 (lower 16 bits): Logarithmic depth encoded to 16 bits
uint PackTileKey(uint tileID, uint depth16)
{
    return (tileID << 16) | (depth16 & 0xFFFF);
}

uint UnpackTileID(uint key)
{
    return key >> 16;
}

uint UnpackDepth16(uint key)
{
    return key & 0xFFFF;
}

// Logarithmic depth encoding for better precision distribution
// More precision near the camera, less far away
uint EncodeDepth16(float linearDepth, float nearPlane, float farPlane)
{
    // Handle edge cases
    if (linearDepth <= nearPlane) return 0;
    if (linearDepth >= farPlane) return 65535;

    // Logarithmic encoding: log2(depth - near + 1) / log2(far - near + 1)
    float logDepth = log2(linearDepth - nearPlane + 1.0) / log2(farPlane - nearPlane + 1.0);
    return uint(saturate(logDepth) * 65535.0);
}

// Convert float depth to sortable uint for ascending radix sort (near-to-far).
// Preserves float ordering: smaller float -> smaller uint.
// IEEE 754 floats: positive floats already sort correctly when reinterpreted as uint,
// but negative floats sort inversely. This transform handles both cases.
uint FloatToSortableUint(float val)
{
    uint u = asuint(val);
    uint mask = (u & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0x80000000u;
    return u ^ mask;
}

// Float16 packing helpers for shared memory optimization in rasterizer
uint PackFloat16x2(float2 v)
{
    return f32tof16(v.x) | (f32tof16(v.y) << 16);
}

float2 UnpackFloat16x2(uint packed)
{
    return float2(f16tof32(packed & 0xFFFF), f16tof32(packed >> 16));
}

#endif // COMPUTE_RASTER_TYPES_H
