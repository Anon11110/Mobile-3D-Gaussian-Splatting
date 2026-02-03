#include "shaderio.h"
#include "compute_raster_types.h"

// SH Constants for degrees 1-3
static const float SH_C1   = 0.4886025119029199;
static const float SH_C2_0 = 1.0925484305920792;
static const float SH_C2_1 = -1.0925484305920792;
static const float SH_C2_2 = 0.31539156525252005;
static const float SH_C2_3 = -1.0925484305920792;
static const float SH_C2_4 = 0.5462742152960396;
static const float SH_C3_0 = -0.5900435899266435;
static const float SH_C3_1 = 2.890611442640554;
static const float SH_C3_2 = -0.4570457994644658;
static const float SH_C3_3 = 0.3731763325901154;
static const float SH_C3_4 = -0.4570457994644658;
static const float SH_C3_5 = 1.445305721320277;
static const float SH_C3_6 = -0.5900435899266435;

[[vk::binding(0, 0)]] ConstantBuffer<FrameUBO> ubo;
[[vk::binding(1, 0)]] StructuredBuffer<float3> positions;
[[vk::binding(2, 0)]] StructuredBuffer<float3> cov3DPacked;
[[vk::binding(3, 0)]] StructuredBuffer<float4> colors;
[[vk::binding(4, 0)]] StructuredBuffer<float> shRest;
[[vk::binding(5, 0)]] StructuredBuffer<uint> meshIndices;
[[vk::binding(6, 0)]] StructuredBuffer<float4x4> modelMatrices;
[[vk::binding(7, 0)]] RWStructuredBuffer<Gaussian2D> geometryBuffer;
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> tileKeys;
[[vk::binding(9, 0)]] RWStructuredBuffer<uint> tileValues;
[[vk::binding(10, 0)]] globallycoherent RWByteAddressBuffer globalCounter;

[[vk::push_constant]] PreprocessPC pc;

void KillSplat(uint splatIndex)
{
    Gaussian2D invalid;
    invalid.screenPos = float2(-10000, -10000);
    invalid.conic = float3(0, 0, 0);
    invalid.opacity = 0;
    invalid.color = float4(0, 0, 0, 0);
    invalid.radius = 0;
    invalid.depth = 0;
    geometryBuffer[splatIndex] = invalid;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    uint splatIndex = dispatchThreadID.x;

    if (splatIndex >= pc.numSplats)
    {
        return;
    }

    float3 localPos = positions[splatIndex];
    float4 baseColor = colors[splatIndex];
    float  opacity = baseColor.a;

    // Early alpha culling
    if (opacity < ubo.alphaCullThreshold)
    {
		KillSplat(splatIndex);
        return;
    }

    float3 covUpper = cov3DPacked[splatIndex * 2];       // M11, M12, M13
    float3 covLower = cov3DPacked[splatIndex * 2 + 1];   // M22, M23, M33

    float3x3 cov3D = float3x3(
        covUpper.x, covUpper.y, covUpper.z,  // Row 1: M11, M12, M13
        covUpper.y, covLower.x, covLower.y,  // Row 2: M12, M22, M23
        covUpper.z, covLower.y, covLower.z   // Row 3: M13, M23, M33
    );

    uint meshIdx = meshIndices[splatIndex];
    float4x4 modelMat = modelMatrices[meshIdx];

    // Transform to world space
    float4 worldPos4 = mul(modelMat, float4(localPos, 1.0));
    float3 worldPos = worldPos4.xyz;

    float3x3 modelRS = float3x3(
        modelMat[0].xyz,
        modelMat[1].xyz,
        modelMat[2].xyz
    );
    cov3D = mul(mul(modelRS, cov3D), transpose(modelRS));

    // Transform to view space
    float4 viewPos4 = mul(ubo.view, float4(worldPos, 1.0));
    float3 viewPos = viewPos4.xyz;
    float linearDepth = -viewPos.z;  // Right-handed: -Z is forward

    // Near plane culling
    if (linearDepth < 0.1)
    {
		KillSplat(splatIndex);
        return;
    }

    // Project to clip/screen space
    float4 clipPos = mul(ubo.projection, viewPos4);
    float2 ndcPos = clipPos.xy / clipPos.w;

    // Frustum culling
    if (any(ndcPos < float2(-1.5, -1.5)) || any(ndcPos > float2(1.5, 1.5)))
    {
		KillSplat(splatIndex);
        return;
    }

    float2 screenPos = (ndcPos * 0.5 + 0.5) * ubo.viewport;

    // Project 3D covariance to 2D (EWA splatting)
    float rz = 1.0 / viewPos.z;
    float rz2 = rz * rz;
    float fx = ubo.focal.x;
    float fy = ubo.focal.y;

    // Jacobian of perspective projection
    float3x3 J = float3x3(
        fx * rz, 0.0,     0.0,
        0.0,     fy * rz, 0.0,
        -(fx * viewPos.x) * rz2, -(fy * viewPos.y) * rz2, 0.0
    );

    // Transform 3D covariance to 2D: cov2D = J * W * cov3D * W^T * J^T
    float3x3 W = (float3x3)ubo.view;
    float3x3 T = mul(mul(W, cov3D), transpose(W));
    float3x3 cov2Dm = mul(mul(J, T), transpose(J));

    // Extract 2x2 covariance (top-left)
    float a = cov2Dm[0][0];
    float b = cov2Dm[0][1];
    float c = cov2Dm[1][1];

    // EWA filtering (anti-aliasing)
    float alphaScale = 1.0;
    if (ubo.enableSplatFilter != 0)
    {
        float detOrig = max(a * c - b * b, 0.0);

        // Apply isotropic low-pass filter
        a += 0.3;
        c += 0.3;

        float detBlur = max(a * c - b * b, 1e-20);
        alphaScale = sqrt(detOrig / detBlur);
        alphaScale = clamp(alphaScale, 0.0, 1.0);
    }
    opacity *= alphaScale;

    // Compute inverse covariance (conic) and eigenvalues for bounding radius
    float det = a * c - b * b;

    if (det <= 1e-6)
    {
        a += 1e-6;
        c += 1e-6;
        det = a * c - b * b;
    }

    if (det <= 0.0)
    {
		KillSplat(splatIndex);
        return;
    }

    // Inverse covariance (conic matrix for quadratic form evaluation)
    float invDet = 1.0 / det;
    float3 conic = float3(c * invDet, -b * invDet, a * invDet);

    // Eigendecomposition for bounding radius
    float mid = 0.5 * (a + c);
    float discriminant = max(0.1, mid * mid - det);
    float lambda1 = mid + sqrt(discriminant);
    float lambda2 = mid - sqrt(discriminant);

    if (lambda2 <= 0.0)
    {
		KillSplat(splatIndex);
        return;
    }

    lambda1 = max(lambda1, 1e-8);
    lambda2 = max(lambda2, 1e-8);

    // Bounding radius: sqrt(8) * sigma for 3-sigma coverage
    float radius1 = sqrt(8.0) * sqrt(lambda1) * ubo.splatScale;
    float radius2 = sqrt(8.0) * sqrt(lambda2) * ubo.splatScale;

    // Cap maximum radius
    float maxPix = ubo.maxSplatRadius;
    radius1 = min(radius1, maxPix);
    radius2 = min(radius2, maxPix);

    float boundingRadius = max(radius1, radius2);

    // Evaluate Spherical Harmonics for view-dependent color
    float3 viewDir = normalize(ubo.cameraPos.xyz - worldPos);
    float x = viewDir.x, y = viewDir.y, z = viewDir.z;

    // Base color (DC coefficient)
    float3 shColor = baseColor.rgb;

    // SH degree 1 (3 coefficients per channel)
    uint offset = splatIndex * 45;
    shColor.r += SH_C1 * (-y * shRest[offset + 0] + z * shRest[offset + 1] - x * shRest[offset + 2]);
    shColor.g += SH_C1 * (-y * shRest[offset + 15] + z * shRest[offset + 16] - x * shRest[offset + 17]);
    shColor.b += SH_C1 * (-y * shRest[offset + 30] + z * shRest[offset + 31] - x * shRest[offset + 32]);

    // SH degree 2 (5 coefficients per channel)
    float xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;
    shColor.r += SH_C2_0 * xy * shRest[offset + 3] +
                 SH_C2_1 * yz * shRest[offset + 4] +
                 SH_C2_2 * (2.0 * zz - xx - yy) * shRest[offset + 5] +
                 SH_C2_3 * xz * shRest[offset + 6] +
                 SH_C2_4 * (xx - yy) * shRest[offset + 7];
    shColor.g += SH_C2_0 * xy * shRest[offset + 18] +
                 SH_C2_1 * yz * shRest[offset + 19] +
                 SH_C2_2 * (2.0 * zz - xx - yy) * shRest[offset + 20] +
                 SH_C2_3 * xz * shRest[offset + 21] +
                 SH_C2_4 * (xx - yy) * shRest[offset + 22];
    shColor.b += SH_C2_0 * xy * shRest[offset + 33] +
                 SH_C2_1 * yz * shRest[offset + 34] +
                 SH_C2_2 * (2.0 * zz - xx - yy) * shRest[offset + 35] +
                 SH_C2_3 * xz * shRest[offset + 36] +
                 SH_C2_4 * (xx - yy) * shRest[offset + 37];

    // SH degree 3 (7 coefficients per channel)
    shColor.r += SH_C3_0 * y * (3.0 * xx - yy) * shRest[offset + 8] +
                 SH_C3_1 * xy * z * shRest[offset + 9] +
                 SH_C3_2 * y * (4.0 * zz - xx - yy) * shRest[offset + 10] +
                 SH_C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * shRest[offset + 11] +
                 SH_C3_4 * x * (4.0 * zz - xx - yy) * shRest[offset + 12] +
                 SH_C3_5 * z * (xx - yy) * shRest[offset + 13] +
                 SH_C3_6 * x * (xx - 3.0 * yy) * shRest[offset + 14];
    shColor.g += SH_C3_0 * y * (3.0 * xx - yy) * shRest[offset + 23] +
                 SH_C3_1 * xy * z * shRest[offset + 24] +
                 SH_C3_2 * y * (4.0 * zz - xx - yy) * shRest[offset + 25] +
                 SH_C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * shRest[offset + 26] +
                 SH_C3_4 * x * (4.0 * zz - xx - yy) * shRest[offset + 27] +
                 SH_C3_5 * z * (xx - yy) * shRest[offset + 28] +
                 SH_C3_6 * x * (xx - 3.0 * yy) * shRest[offset + 29];
    shColor.b += SH_C3_0 * y * (3.0 * xx - yy) * shRest[offset + 38] +
                 SH_C3_1 * xy * z * shRest[offset + 39] +
                 SH_C3_2 * y * (4.0 * zz - xx - yy) * shRest[offset + 40] +
                 SH_C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * shRest[offset + 41] +
                 SH_C3_4 * x * (4.0 * zz - xx - yy) * shRest[offset + 42] +
                 SH_C3_5 * z * (xx - yy) * shRest[offset + 43] +
                 SH_C3_6 * x * (xx - 3.0 * yy) * shRest[offset + 44];

    shColor = max(shColor, float3(0.0, 0.0, 0.0));

    // Write Gaussian2D to geometry buffer
    Gaussian2D g;
    g.screenPos = screenPos;
    g.conic = conic;
    g.opacity = opacity;
    g.color = float4(shColor, 1.0);
    g.radius = boundingRadius;
    g.depth = linearDepth;
    geometryBuffer[splatIndex] = g;

    // Calculate touched tiles and emit key-value pairs
    float tileSize = (float)pc.tileSize;

    // Bounding box in tile coordinates
    int minTileX = max(0, int(floor((screenPos.x - boundingRadius) / tileSize)));
    int maxTileX = min((int)pc.tilesX - 1, int(floor((screenPos.x + boundingRadius) / tileSize)));
    int minTileY = max(0, int(floor((screenPos.y - boundingRadius) / tileSize)));
    int maxTileY = min((int)pc.tilesY - 1, int(floor((screenPos.y + boundingRadius) / tileSize)));

    // Count tiles this splat touches
    uint numTilesTouched = 0;
    if (minTileX <= maxTileX && minTileY <= maxTileY)
    {
        numTilesTouched = (maxTileX - minTileX + 1) * (maxTileY - minTileY + 1);
    }

    bool shouldEmit = (numTilesTouched > 0);

    uint waveTotal = WaveActiveSum(shouldEmit ? numTilesTouched : 0);
    uint waveOffset = 0;

    if (WaveIsFirstLane())
    {
        globalCounter.InterlockedAdd(0, waveTotal, waveOffset);
    }
    waveOffset = WaveReadLaneFirst(waveOffset);

    // Each thread's offset within the wave allocation
    uint myOffset = waveOffset + WavePrefixSum(shouldEmit ? numTilesTouched : 0);

    // Emit tile-key pairs
    if (shouldEmit)
    {
        // Encode depth using logarithmic scale for better precision
        uint depth16 = EncodeDepth16(linearDepth, pc.nearPlane, pc.farPlane);

        uint localIdx = 0;
        for (int ty = minTileY; ty <= maxTileY; ty++)
        {
            for (int tx = minTileX; tx <= maxTileX; tx++)
            {
                uint tileID = ty * pc.tilesX + tx;
                uint writeIdx = myOffset + localIdx;

                if (writeIdx < pc.maxTileInstances)
                {
                    // Pack key: (TileID << 16) | Depth16
                    tileKeys[writeIdx] = PackTileKey(tileID, depth16);
                    tileValues[writeIdx] = splatIndex;
                }

                localIdx++;
            }
        }
    }
}
