// Each workgroup processes one 16x16 tile, with one thread per pixel.
#include "compute_raster_types.h"

[[vk::binding(0, 0)]] StructuredBuffer<Gaussian2D> geometryBuffer;
[[vk::binding(1, 0)]] StructuredBuffer<uint> sortedTileValues;
[[vk::binding(2, 0)]] StructuredBuffer<int2> tileRanges;
[[vk::binding(3, 0)]] RWTexture2D<float4> outputImage;

[[vk::push_constant]] RasterPC pc;

struct GaussianShared
{
    float2 screenPos;
    uint   conicXY;
    uint   conicZOpacity;
    uint   colorRG;
    uint   colorBA;
};

#define TILE_SIZE 16
#define BATCH_SIZE 256

groupshared GaussianShared sharedSplats[BATCH_SIZE];

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint3 globalID : SV_DispatchThreadID)
{
    uint2 tileCoord  = groupID.xy;
    uint2 pixelCoord = globalID.xy;

    uint lid = localID.y * TILE_SIZE + localID.x;
    uint flatTileID = tileCoord.y * pc.tilesX + tileCoord.x;

    bool pixelInBounds = (pixelCoord.x < pc.screenWidth && pixelCoord.y < pc.screenHeight);

    int2 range = tileRanges[flatTileID];
    int rangeStart = range.x;
    int rangeEnd   = range.y;

    float3 accum = float3(0, 0, 0);
    float T = 1.0;

    if (rangeStart >= 0 && rangeEnd > rangeStart)
    {
        for (int batchStart = rangeStart; batchStart < rangeEnd; batchStart += BATCH_SIZE)
        {
            int loadIdx = batchStart + lid;
            if (loadIdx < rangeEnd && lid < BATCH_SIZE)
            {
                uint splatIdx = sortedTileValues[loadIdx];
                Gaussian2D g = geometryBuffer[splatIdx];

                GaussianShared gs;
                gs.screenPos     = g.screenPos;
                gs.conicXY       = PackFloat16x2(g.conic.xy);
                gs.conicZOpacity = PackFloat16x2(float2(g.conic.z, g.opacity));
                gs.colorRG       = PackFloat16x2(g.color.rg);
                gs.colorBA       = PackFloat16x2(g.color.ba);
                sharedSplats[lid] = gs;
            }
            GroupMemoryBarrierWithGroupSync();

            int batchEnd = min(batchStart + BATCH_SIZE, rangeEnd);
            int batchCount = batchEnd - batchStart;

            // Each thread processes all splats in batch for its pixel
            if (pixelInBounds)
            {
                float2 pixelCenter = float2(pixelCoord) + 0.5;

                for (int i = 0; i < batchCount; i++)
                {
                    // Early exit when pixel is fully opaque
                    if (T < 1.0 / 255.0) break;

                    GaussianShared gs = sharedSplats[i];

                    // Compute offset from pixel center to splat center
                    float2 d = pixelCenter - gs.screenPos;

                    float2 conicXY = UnpackFloat16x2(gs.conicXY);
                    float2 conicZOpacity = UnpackFloat16x2(gs.conicZOpacity);

                    float conicA = conicXY.x;
                    float conicB = conicXY.y;
                    float conicC = conicZOpacity.x;
                    float splatOpacity = conicZOpacity.y;

                    // Evaluate Gaussian: power = -0.5 * (d^T * Sigma^{-1} * d)
                    float power = -0.5 * (conicA * d.x * d.x + 2.0 * conicB * d.x * d.y + conicC * d.y * d.y);

                    // Outside ~3 sigma
                    if (power > 0.0) continue;

                    float alpha = min(0.99, splatOpacity * exp(power));
                    if (alpha < 1.0 / 255.0) continue;

                    // Unpack color
                    float2 colorRG = UnpackFloat16x2(gs.colorRG);
                    float2 colorBA = UnpackFloat16x2(gs.colorBA);

                    // Alpha compositing
                    accum += T * alpha * float3(colorRG, colorBA.x);
                    T *= (1.0 - alpha);
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }

    if (pixelInBounds)
    {
        outputImage[pixelCoord] = float4(accum, 1.0 - T);
    }
}
