// Each workgroup processes one 16x16 tile, with one thread per pixel.
#include "compute_raster_types.h"

[[vk::binding(0, 0)]] StructuredBuffer<Gaussian2D> geometryBuffer;
[[vk::binding(1, 0)]] StructuredBuffer<uint> sortedTileValues;
[[vk::binding(2, 0)]] StructuredBuffer<int2> tileRanges;
[[vk::binding(3, 0)]] RWTexture2D<float4> outputImage;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> transmittanceStats;  // [0]=totalEvals, [1]=actualEvals, [2]=earlyExitPixels

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

groupshared uint sharedTotalEvals;
groupshared uint sharedActualEvals;
groupshared uint sharedEarlyExitPixels;

// Green -> Yellow -> Red heatmap based on savings ratio [0, 1]
float3 SavingsHeatmap(float ratio)
{
    // green = no savings, yellow = medium, red = all skipped
    float3 green  = float3(0.0, 0.8, 0.2);
    float3 yellow = float3(1.0, 0.9, 0.1);
    float3 red    = float3(0.9, 0.1, 0.0);

    if (ratio < 0.5)
        return lerp(green, yellow, ratio * 2.0);
    else
        return lerp(yellow, red, (ratio - 0.5) * 2.0);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint3 globalID : SV_DispatchThreadID)
{
    uint2 tileCoord  = groupID.xy;
    uint2 pixelCoord = globalID.xy;

    uint lid = localID.y * TILE_SIZE + localID.x;
    uint flatTileID = tileCoord.y * pc.tilesX + tileCoord.x;

    bool pixelInBounds = (pixelCoord.x < pc.screenWidth && pixelCoord.y < pc.screenHeight);
    bool statsEnabled = (pc.transmittanceStatsMode > 0);

    int2 range = tileRanges[flatTileID];
    int rangeStart = range.x;
    int rangeEnd   = range.y;

    float3 accum = float3(0, 0, 0);
    float T = 1.0;

    // Per-pixel stats tracking
    uint localTotal     = 0;
    uint localProcessed = 0;
    bool didEarlyExit   = false;

    if (rangeStart >= 0 && rangeEnd > rangeStart)
    {
        localTotal = uint(rangeEnd - rangeStart);

        for (int batchStart = rangeStart; batchStart < rangeEnd; batchStart += BATCH_SIZE)
        {
            int loadIdx = batchStart + lid;
            if (loadIdx < rangeEnd && lid < BATCH_SIZE)
            {
                uint splatIdx = sortedTileValues[loadIdx];
                Gaussian2D g = geometryBuffer[splatIdx];

                GaussianShared gs;
                gs.screenPos     = g.screenPos;
                gs.conicXY       = PackFloat16x2(g.conicOpacity.xy);
                gs.conicZOpacity = PackFloat16x2(g.conicOpacity.zw);  // conic.z + opacity
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
                    if (T < 1.0 / 255.0)
                    {
                        didEarlyExit = true;
                        break;
                    }

                    localProcessed++;

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

    // Transmittance stats
    if (statsEnabled)
    {
        if (lid == 0)
        {
            sharedTotalEvals = 0;
            sharedActualEvals = 0;
            sharedEarlyExitPixels = 0;
        }
        GroupMemoryBarrierWithGroupSync();

        // Each thread contributes its local stats
        if (pixelInBounds)
        {
            InterlockedAdd(sharedTotalEvals, localTotal);
            InterlockedAdd(sharedActualEvals, localProcessed);
            if (didEarlyExit)
                InterlockedAdd(sharedEarlyExitPixels, 1u);
        }
        GroupMemoryBarrierWithGroupSync();

        // Thread 0 writes tile totals to global buffer
        if (lid == 0)
        {
            InterlockedAdd(transmittanceStats[0], sharedTotalEvals);
            InterlockedAdd(transmittanceStats[1], sharedActualEvals);
            InterlockedAdd(transmittanceStats[2], sharedEarlyExitPixels);
        }
    }

    if (pixelInBounds)
    {
        // Heatmap mode: visualize per-pixel savings ratio
        if (pc.transmittanceStatsMode == 2 && localTotal > 0)
        {
            float savingsRatio = 1.0 - float(localProcessed) / float(localTotal);
            float3 heatColor = SavingsHeatmap(savingsRatio);
            outputImage[pixelCoord] = float4(heatColor, 1.0);
        }
        else
        {
            outputImage[pixelCoord] = float4(accum, 1.0 - T);
        }
    }
}
