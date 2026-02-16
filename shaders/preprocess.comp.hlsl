#include "shaderio.h"
#include "compute_raster_types.h"

// SH Constants for degrees 1-3
static const half SH_C1   = half(0.4886025119029199);
static const half SH_C2_0 = half(1.0925484305920792);
static const half SH_C2_1 = half(-1.0925484305920792);
static const half SH_C2_2 = half(0.31539156525252005);
static const half SH_C2_3 = half(-1.0925484305920792);
static const half SH_C2_4 = half(0.5462742152960396);
static const half SH_C3_0 = half(-0.5900435899266435);
static const half SH_C3_1 = half(2.890611442640554);
static const half SH_C3_2 = half(-0.4570457994644658);
static const half SH_C3_3 = half(0.3731763325901154);
static const half SH_C3_4 = half(-0.4570457994644658);
static const half SH_C3_5 = half(1.445305721320277);
static const half SH_C3_6 = half(-0.5900435899266435);

[[vk::binding(0, 0)]] ConstantBuffer<FrameUBO> ubo;
[[vk::binding(1, 0)]] StructuredBuffer<float4> positions;
[[vk::binding(2, 0)]] StructuredBuffer<float4> cov3DPacked;   // 2 float4 per splat (float32, 32 bytes)
[[vk::binding(3, 0)]] StructuredBuffer<uint2> colorsHalf;     // 1 uint2 per splat: {R|G, B|A} (8 bytes)
[[vk::binding(4, 0)]] StructuredBuffer<uint4> shRestInterleaved; // 6 uint4 per splat, interleaved RGB (96 bytes)
[[vk::binding(5, 0)]] StructuredBuffer<uint> meshIndices;
[[vk::binding(6, 0)]] StructuredBuffer<float4x4> modelMatrices;
[[vk::binding(7, 0)]] RWStructuredBuffer<Gaussian2D> geometryBuffer;
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> tileKeys;
[[vk::binding(9, 0)]] RWStructuredBuffer<uint> tileValues;
[[vk::binding(10, 0)]] globallycoherent RWByteAddressBuffer globalCounter;

[[vk::push_constant]] PreprocessPC pc;

float2 UnpackHalf2x16(uint packed)
{
    return float2(f16tof32(packed), f16tof32(packed >> 16));
}

#define HALF_LO(x) half(f16tof32(x))
#define HALF_HI(x) half(f16tof32((x) >> 16))

// Interleaved SH buffer: 6 uint4 per splat
static const uint SH_UINT4_PER_SPLAT = 6;


void KillSplat(uint splatIndex)
{
    Gaussian2D invalid;
    invalid.conicOpacity = float4(0, 0, 0, 0);  // conic.xyz + opacity in w
    invalid.color = float4(0, 0, 0, 0);
    invalid.screenPos = float2(-10000, -10000);
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

    float3 localPos = positions[splatIndex].xyz;

    // Unpack color from half-precision
    uint2 colorPacked = colorsHalf[splatIndex];
    half4 baseColor = half4(
        half2(UnpackHalf2x16(colorPacked.x)),
        half2(UnpackHalf2x16(colorPacked.y)));
    half  opacity = baseColor.a;

    // Early alpha culling
    if (float(opacity) < ubo.alphaCullThreshold)
    {
		KillSplat(splatIndex);
        return;
    }

    uint meshIdx = meshIndices[splatIndex];
    float4x4 modelMat = modelMatrices[meshIdx];

    // Transform position to world space
    float4 worldPos4 = mul(modelMat, float4(localPos, 1.0));
    float3 worldPos = worldPos4.xyz;

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

    float3 covUpper = cov3DPacked[splatIndex * 2].xyz;       // M11, M12, M13
    float3 covLower = cov3DPacked[splatIndex * 2 + 1].xyz;   // M22, M23, M33

    float3x3 cov3D = float3x3(
        covUpper.x, covUpper.y, covUpper.z,  // Row 1: M11, M12, M13
        covUpper.y, covLower.x, covLower.y,  // Row 2: M12, M22, M23
        covUpper.z, covLower.y, covLower.z   // Row 3: M13, M23, M33
    );

    float3x3 modelRS = float3x3(
        modelMat[0].xyz,
        modelMat[1].xyz,
        modelMat[2].xyz
    );
    cov3D = mul(mul(modelRS, cov3D), transpose(modelRS));

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
    opacity *= half(alphaScale);

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
    half x = half(viewDir.x), y = half(viewDir.y), z = half(viewDir.z);
    half3 shColor = baseColor.rgb;

    uint shBase = splatIndex * SH_UINT4_PER_SPLAT;

    // SH Degree 1: load d0,d1 → extract sh[0..2]
    uint4 d0 = shRestInterleaved[shBase + 0];
    uint4 d1 = shRestInterleaved[shBase + 1];

    shColor += SH_C1 * (-y * half3(HALF_LO(d0.x), HALF_HI(d0.x), HALF_LO(d0.y))
                        + z * half3(HALF_HI(d0.y), HALF_LO(d0.z), HALF_HI(d0.z))
                        - x * half3(HALF_LO(d0.w), HALF_HI(d0.w), HALF_LO(d1.x)));

    // SH Degree 2: reuse d1, load d2 → extract sh[3..7]
    uint4 d2 = shRestInterleaved[shBase + 2];
    half xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;

    shColor += SH_C2_0 * xy * half3(HALF_HI(d1.x), HALF_LO(d1.y), HALF_HI(d1.y)) +
               SH_C2_1 * yz * half3(HALF_LO(d1.z), HALF_HI(d1.z), HALF_LO(d1.w)) +
               SH_C2_2 * (half(2.0) * zz - xx - yy) * half3(HALF_HI(d1.w), HALF_LO(d2.x), HALF_HI(d2.x)) +
               SH_C2_3 * xz * half3(HALF_LO(d2.y), HALF_HI(d2.y), HALF_LO(d2.z)) +
               SH_C2_4 * (xx - yy) * half3(HALF_HI(d2.z), HALF_LO(d2.w), HALF_HI(d2.w));

    // SH Degree 3: load d3,d4,d5 → extract sh[8..14]
    uint4 d3 = shRestInterleaved[shBase + 3];
    uint4 d4 = shRestInterleaved[shBase + 4];
    uint4 d5 = shRestInterleaved[shBase + 5];

    shColor += SH_C3_0 * y * (half(3.0) * xx - yy) * half3(HALF_LO(d3.x), HALF_HI(d3.x), HALF_LO(d3.y)) +
               SH_C3_1 * xy * z * half3(HALF_HI(d3.y), HALF_LO(d3.z), HALF_HI(d3.z)) +
               SH_C3_2 * y * (half(4.0) * zz - xx - yy) * half3(HALF_LO(d3.w), HALF_HI(d3.w), HALF_LO(d4.x)) +
               SH_C3_3 * z * (half(2.0) * zz - half(3.0) * xx - half(3.0) * yy) * half3(HALF_HI(d4.x), HALF_LO(d4.y), HALF_HI(d4.y)) +
               SH_C3_4 * x * (half(4.0) * zz - xx - yy) * half3(HALF_LO(d4.z), HALF_HI(d4.z), HALF_LO(d4.w)) +
               SH_C3_5 * z * (xx - yy) * half3(HALF_HI(d4.w), HALF_LO(d5.x), HALF_HI(d5.x)) +
               SH_C3_6 * x * (xx - half(3.0) * yy) * half3(HALF_LO(d5.y), HALF_HI(d5.y), HALF_LO(d5.z));

    shColor = max(shColor, half3(0.0, 0.0, 0.0));

    // Write Gaussian2D to geometry buffer
    Gaussian2D g;
    g.conicOpacity = float4(conic, float(opacity));
    g.color = float4(float3(shColor), 1.0);
    g.screenPos = screenPos;
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
