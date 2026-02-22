#include "shaderio.h"

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
[[vk::binding(7, 0)]] RWStructuredBuffer<HWRasterSplat> preprocessedSplats;
[[vk::binding(8, 0)]] globallycoherent RWByteAddressBuffer atomicCounter;
[[vk::binding(9, 0)]] RWStructuredBuffer<uint2> sortPairsOutput;
[[vk::binding(10, 0)]] RWByteAddressBuffer indirectArgs;

struct SplatPrecomputePC
{
    uint numElements;
    uint sortAscending;  // 0 = far-to-near, 1 = near-to-far
    uint chunkCount;
    uint totalWorkgroups;
};
[[vk::push_constant]] SplatPrecomputePC pc;

// Indirect args constants, must match IndirectArgsBuffer layout on CPU side
#define SORT_WORKGROUP_SIZE 256
#define MAX_SORT_WORKGROUPS 256
#define RADIX_SORT_BINS 256
#define ELEMENTS_PER_THREAD 4
#define MAX_CHUNKS 8
#define CHUNK_DRAW_OFFSET 100
#define DRAW_CMD_SIZE 20

float2 UnpackHalf2x16(uint packed)
{
    return float2(f16tof32(packed), f16tof32(packed >> 16));
}

#define HALF_LO(x) half(f16tof32(x))
#define HALF_HI(x) half(f16tof32((x) >> 16))

// Interleaved SH buffer: 6 uint4 per splat
static const uint SH_UINT4_PER_SPLAT = 6;

// Converts a float to a sortable uint for radix sort.
// When sortAscending=0: larger Z -> smaller uint key (far-to-near after ascending radix sort)
// When sortAscending=1: smaller Z -> smaller uint key (near-to-far after ascending radix sort)
uint FloatToSortableUint(float val, uint sortAscending)
{
    uint u = asuint(val);
    uint mask = (u & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0x80000000u;

    if (sortAscending != 0)
    {
        return u ^ mask;
    }
    else
    {
        return 0xFFFFFFFFu - (u ^ mask);
    }
}

// Struct for 2D covariance with alpha compensation
struct Covariance2D
{
    float3 cov2d;      // (a, b, c), upper triangle of 2x2 matrix
    float  alphaScale; // determinant-based AA compensation
};

// Projects the 3D covariance to 2D with Mip-Splat AA support
Covariance2D ProjectCovariance3D(float3x3 cov3D, float3 viewPos)
{
    Covariance2D result;

    float rz  = 1.0 / viewPos.z;
    float rz2 = rz * rz;
    float fx  = ubo.focal.x;
    float fy  = ubo.focal.y;

    float3x3 J = float3x3(
        fx * rz, 0.0,     0.0,
        0.0,     fy * rz, 0.0,
        -(fx * viewPos.x) * rz2, -(fy * viewPos.y) * rz2, 0.0
    );

    // Transform 3D covariance to 2D
    float3x3 W      = (float3x3)ubo.view;
    float3x3 T      = mul(mul(W, cov3D), transpose(W));
    float3x3 cov2Dm = mul(mul(J, T), transpose(J));

    // Compute determinant before blur (top-left 2x2)
    float a = cov2Dm[0][0];
    float b = cov2Dm[0][1];
    float c = cov2Dm[1][1];

    // Compute determinant after blur
    float a_blur = cov2Dm[0][0];
    float b_blur = cov2Dm[0][1];
    float c_blur = cov2Dm[1][1];

    // EWA filtering to make splat wider and darker
    float alphaScale = 1.0;
    if (ubo.enableSplatFilter != 0)
    {
        // Apply isotropic low-pass filter for anti-aliasing
        cov2Dm[0][0] += 0.3;
        cov2Dm[1][1] += 0.3;

        a_blur = cov2Dm[0][0];
        c_blur = cov2Dm[1][1];

        float detOrig = max(a * c - b * b, 0.0);
        float detBlur = max(a_blur * c_blur - b_blur * b_blur, 1e-20);
        alphaScale = sqrt(detOrig / detBlur);
        alphaScale = clamp(alphaScale, 0.0, 1.0);
    }

    result.cov2d      = float3(a_blur, b_blur, c_blur);
    result.alphaScale = alphaScale;
    return result;
}

// Calculates the basis vectors and radii for the 2D ellipse with radius caps
bool GetEllipseBasis(float3 cov2d, out float2 axis1, out float2 axis2, out float radius1, out float radius2)
{
    float a = cov2d.x;
    float b = cov2d.y;
    float c = cov2d.z;

    float det = a * c - b * b;

    if (det <= 1e-6)
    {
        // Add a small epsilon to the diagonal to make it invertible
        a += 1e-6;
        c += 1e-6;
        det = a * c - b * b;
    }
    if (det <= 0.0)
    {
        axis1 = float2(0, 0);
        axis2 = float2(0, 0);
        radius1 = 0;
        radius2 = 0;
        return false;
    }

    // Eigendecomposition
    float mid          = 0.5 * (a + c);
    float discriminant = max(0.1, mid * mid - det);
    float lambda1      = mid + sqrt(discriminant);
    float lambda2      = mid - sqrt(discriminant);

    // Check for eigenvalue degeneracy
    if (lambda2 <= 0.0)
    {
        axis1 = float2(0, 0);
        axis2 = float2(0, 0);
        radius1 = 0;
        radius2 = 0;
        return false;
    }

    lambda1 = max(lambda1, 1e-8);
    lambda2 = max(lambda2, 1e-8);

    // Radii in pixels (sqrt(8) * sigma for 3-sigma coverage) to ensure the Gaussian fades to ~1.8% at the edge
    radius1 = sqrt(8.0) * sqrt(lambda1);
    radius2 = sqrt(8.0) * sqrt(lambda2);

    // Apply splat scale factor
    radius1 *= ubo.splatScale;
    radius2 *= ubo.splatScale;

    // Cap maximum radius to prevent huge splats
    float maxPix = ubo.maxSplatRadius;
    radius1 = min(radius1, maxPix);
    radius2 = min(radius2, maxPix);

    float two_b = 2.0 * b;
    float diff  = a - c;
    float r     = length(float2(two_b, diff));
    r           = max(r, 1e-20);

    float cosT = sqrt(max(0.0, (r + diff) / (2.0 * r)));
    float sinT = sqrt(max(0.0, (r - diff) / (2.0 * r)));
    sinT       = (b >= 0.0) ? sinT : -sinT;

    axis1 = float2(cosT, sinT);
    axis2 = float2(-axis1.y, axis1.x);

    return true;
}

// Checks if the splat is outside the view frustum
bool IsCulledByFrustum(float4 centerClip, float radius1, float radius2)
{
    float2 centerNDC = centerClip.xy / centerClip.w;
    float  maxRadius = max(radius1, radius2);
    float2 ndcMargin = (maxRadius / ubo.viewport) * 2.0;

    return any(centerNDC < float2(-1.3, -1.3) - ndcMargin) ||
           any(centerNDC > float2(1.3, 1.3) + ndcMargin);
}

// SH evaluation function for degrees 0-3
float3 ComputeSH(uint splatIndex, float3 dir, half3 baseColor)
{
    half x = half(dir.x), y = half(dir.y), z = half(dir.z);
    half3 result = baseColor;

    uint base = splatIndex * SH_UINT4_PER_SPLAT;

    // SH Degree 1: load d0,d1 -> extract sh[0..2]
    uint4 d0 = shRestInterleaved[base + 0];
    uint4 d1 = shRestInterleaved[base + 1];

    result += SH_C1 * (-y * half3(HALF_LO(d0.x), HALF_HI(d0.x), HALF_LO(d0.y))
                       + z * half3(HALF_HI(d0.y), HALF_LO(d0.z), HALF_HI(d0.z))
                       - x * half3(HALF_LO(d0.w), HALF_HI(d0.w), HALF_LO(d1.x)));

    // SH Degree 2: reuse d1, load d2 -> extract sh[3..7]
    uint4 d2 = shRestInterleaved[base + 2];
    half xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;

    result += SH_C2_0 * xy * half3(HALF_HI(d1.x), HALF_LO(d1.y), HALF_HI(d1.y)) +
              SH_C2_1 * yz * half3(HALF_LO(d1.z), HALF_HI(d1.z), HALF_LO(d1.w)) +
              SH_C2_2 * (half(2.0) * zz - xx - yy) * half3(HALF_HI(d1.w), HALF_LO(d2.x), HALF_HI(d2.x)) +
              SH_C2_3 * xz * half3(HALF_LO(d2.y), HALF_HI(d2.y), HALF_LO(d2.z)) +
              SH_C2_4 * (xx - yy) * half3(HALF_HI(d2.z), HALF_LO(d2.w), HALF_HI(d2.w));

    // SH Degree 3: load d3,d4,d5 -> extract sh[8..14]
    uint4 d3 = shRestInterleaved[base + 3];
    uint4 d4 = shRestInterleaved[base + 4];
    uint4 d5 = shRestInterleaved[base + 5];

    result += SH_C3_0 * y * (half(3.0) * xx - yy) * half3(HALF_LO(d3.x), HALF_HI(d3.x), HALF_LO(d3.y)) +
              SH_C3_1 * xy * z * half3(HALF_HI(d3.y), HALF_LO(d3.z), HALF_HI(d3.z)) +
              SH_C3_2 * y * (half(4.0) * zz - xx - yy) * half3(HALF_LO(d3.w), HALF_HI(d3.w), HALF_LO(d4.x)) +
              SH_C3_3 * z * (half(2.0) * zz - half(3.0) * xx - half(3.0) * yy) * half3(HALF_HI(d4.x), HALF_LO(d4.y), HALF_HI(d4.y)) +
              SH_C3_4 * x * (half(4.0) * zz - xx - yy) * half3(HALF_LO(d4.z), HALF_HI(d4.z), HALF_LO(d4.w)) +
              SH_C3_5 * z * (xx - yy) * half3(HALF_HI(d4.w), HALF_LO(d5.x), HALF_HI(d5.x)) +
              SH_C3_6 * x * (xx - half(3.0) * yy) * half3(HALF_LO(d5.y), HALF_HI(d5.y), HALF_LO(d5.z));

    return float3(max(result, half3(0.0, 0.0, 0.0)));
}

// Helper to fetch pre-computed 3D covariance matrix from buffer
float3x3 GetCovariance3D(uint splatIndex)
{
    float3 upper = cov3DPacked[splatIndex * 2].xyz;       // M11, M12, M13
    float3 lower = cov3DPacked[splatIndex * 2 + 1].xyz;   // M22, M23, M33

    return float3x3(
        upper.x, upper.y, upper.z,   // Row 1: M11, M12, M13
        upper.y, lower.x, lower.y,   // Row 2: M12, M22, M23
        upper.z, lower.y, lower.z    // Row 3: M13, M23, M33
    );
}

// Writes all indirect dispatch/draw commands for the GPU-driven sort pipeline.
// Called by thread 0 of the last workgroup to complete.
//
// IndirectArgsBuffer layout:
//   Offset  0: SortParams {numElements, numWorkgroups, numBlocksPerWorkgroup, numScanElements, numScanWorkgroups} (20B, pad 32)
//   Offset 32: DispatchIndirect for histogram/scatter: {numWorkgroups, 1, 1}
//   Offset 44: DispatchIndirect for scan blocks/add offsets: {numScanWorkgroups, 1, 1}
//   Offset 56: DispatchIndirect for scan block sums: {1, 1, 1}
//   Offset 68: DispatchIndirect for unpack: {ceil(V/256), 1, 1}
//   Offset 80: DrawIndexedIndirectCommand: {4, visibleCount, 0, 0, 0}
//   Offset 100: Per-chunk DrawIndexedIndirectCommands for transmittance culling (up to MAX_CHUNKS)
void WriteIndirectArgs(uint visibleCount)
{
    // Compute sort dispatch parameters
    uint numWorkgroups = (visibleCount + SORT_WORKGROUP_SIZE - 1) / SORT_WORKGROUP_SIZE;
    numWorkgroups = min(numWorkgroups, MAX_SORT_WORKGROUPS);
    numWorkgroups = max(numWorkgroups, 1);

    uint elementsPerWorkgroup  = (visibleCount + numWorkgroups - 1) / numWorkgroups;
    uint numBlocksPerWorkgroup = (elementsPerWorkgroup + SORT_WORKGROUP_SIZE - 1) / SORT_WORKGROUP_SIZE;
    numBlocksPerWorkgroup = max(numBlocksPerWorkgroup, 1);

    uint numScanElements   = numWorkgroups * RADIX_SORT_BINS;
    uint elementsPerScanWG = SORT_WORKGROUP_SIZE * ELEMENTS_PER_THREAD;
    uint numScanWorkgroups = (numScanElements + elementsPerScanWG - 1) / elementsPerScanWG;
    numScanWorkgroups = max(numScanWorkgroups, 1);

    // SortParams at offset 0
    indirectArgs.Store(0,  visibleCount);
    indirectArgs.Store(4,  numWorkgroups);
    indirectArgs.Store(8,  numBlocksPerWorkgroup);
    indirectArgs.Store(12, numScanElements);
    indirectArgs.Store(16, numScanWorkgroups);

    // DispatchIndirect for histogram/scatter at offset 32
    indirectArgs.Store(32, numWorkgroups);
    indirectArgs.Store(36, 1);
    indirectArgs.Store(40, 1);

    // DispatchIndirect for scan blocks/add offsets at offset 44
    indirectArgs.Store(44, numScanWorkgroups);
    indirectArgs.Store(48, 1);
    indirectArgs.Store(52, 1);

    // DispatchIndirect for scan block sums at offset 56
    indirectArgs.Store(56, 1);
    indirectArgs.Store(60, 1);
    indirectArgs.Store(64, 1);

    // DispatchIndirect for unpack at offset 68
    uint unpackGroups = (visibleCount + SORT_WORKGROUP_SIZE - 1) / SORT_WORKGROUP_SIZE;
    unpackGroups = max(unpackGroups, 1);
    indirectArgs.Store(68, unpackGroups);
    indirectArgs.Store(72, 1);
    indirectArgs.Store(76, 1);

    // DrawIndexedIndirectCommand at offset 80: {indexCount, instanceCount, firstIndex, vertexOffset, firstInstance}
    indirectArgs.Store(80, 4);
    indirectArgs.Store(84, visibleCount);
    indirectArgs.Store(88, 0);
    indirectArgs.Store(92, 0);
    indirectArgs.Store(96, 0);

    // Per-chunk DrawIndexedIndirectCommands for transmittance culling at offset 100
    uint chunkCount     = min(pc.chunkCount, MAX_CHUNKS);
    uint splatsPerChunk = (visibleCount + chunkCount - 1) / max(chunkCount, 1);

    for (uint i = 0; i < MAX_CHUNKS; ++i)
    {
        uint offset    = CHUNK_DRAW_OFFSET + i * DRAW_CMD_SIZE;
        uint firstSplat = i * splatsPerChunk;

        if (i < chunkCount && firstSplat < visibleCount)
        {
            uint count = min(splatsPerChunk, visibleCount - firstSplat);
            indirectArgs.Store(offset,      4);
            indirectArgs.Store(offset + 4,  count);
            indirectArgs.Store(offset + 8,  0);
            indirectArgs.Store(offset + 12, 0);
            indirectArgs.Store(offset + 16, firstSplat);
        }
        else
        {
            // Zero-instance draw
            indirectArgs.Store(offset,      4);
            indirectArgs.Store(offset + 4,  0);
            indirectArgs.Store(offset + 8,  0);
            indirectArgs.Store(offset + 12, 0);
            indirectArgs.Store(offset + 16, 0);
        }
    }
}

groupshared uint s_completedGroups;

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint gtid : SV_GroupIndex)
{
    uint splatID = dispatchThreadId.x;
    bool isVisible = false;

    // Per-splat outputs
    float4 splatCenterClip;
    float2 splatNdcBasis1;
    float2 splatNdcBasis2;
    float4 splatColor;
    float  splatDepth;

    if (splatID < pc.numElements)
    {
        // Load position and transform to world space
        float3 localCenter = positions[splatID].xyz;
        uint meshIdx       = meshIndices[splatID];
        float4x4 modelMat  = modelMatrices[meshIdx];
        float4 worldPos4   = mul(modelMat, float4(localCenter, 1.0));
        float3 center      = worldPos4.xyz;

        // Transform to view space
        float4 viewPos4 = mul(ubo.view, float4(center, 1.0));
        float3 viewPos  = viewPos4.xyz;

        // Unpack color from half-precision
        uint2 colorPacked = colorsHalf[splatID];
        half4 baseColor = half4(
            half2(UnpackHalf2x16(colorPacked.x)),
            half2(UnpackHalf2x16(colorPacked.y)));

        // Alpha and depth culling
        if (baseColor.a >= ubo.alphaCullThreshold && viewPos.z <= -0.1)
        {
            // Transform covariance to world space
            float3x3 cov3D = GetCovariance3D(splatID);
            float3x3 modelRS = float3x3(
                modelMat[0].xyz,
                modelMat[1].xyz,
                modelMat[2].xyz
            );
            cov3D = mul(mul(modelRS, cov3D), transpose(modelRS));

            // SH evaluation
            float3 viewDir = normalize(ubo.cameraPos.xyz - center);
            float3 shColor = ComputeSH(splatID, viewDir, baseColor.rgb);
            float4 color   = float4(shColor, float(baseColor.a));

            // Covariance projection + EWA filter
            Covariance2D cov_out = ProjectCovariance3D(cov3D, viewPos);
            color.a *= cov_out.alphaScale;

            // Eigendecomposition
            float2 axis1, axis2;
            float  radius1, radius2;

            if (GetEllipseBasis(cov_out.cov2d, axis1, axis2, radius1, radius2))
            {
                // Frustum culling
                float4 centerClip = mul(ubo.projection, viewPos4);

                if (!IsCulledByFrustum(centerClip, radius1, radius2))
                {
                    isVisible = true;

                    // Pre-compute NDC basis vectors
                    float2 basis1 = radius1 * axis1;
                    float2 basis2 = radius2 * axis2;

                    float2x2 rotMat = float2x2(ubo.screenRotation.xy, ubo.screenRotation.zw);

                    splatCenterClip = centerClip;
                    splatNdcBasis1  = mul(rotMat, basis1) * ubo.basisViewport * 2.0 * ubo.inverseFocalAdj;
                    splatNdcBasis2  = mul(rotMat, basis2) * ubo.basisViewport * 2.0 * ubo.inverseFocalAdj;
                    splatColor      = color;
                    splatDepth      = -viewPos.z;
                }
            }
        }
    }

    // Stream compaction, only visible ones contribute
    uint waveVisible = WaveActiveCountBits(isVisible);
    uint waveOffset  = 0;
    if (WaveIsFirstLane())
    {
        atomicCounter.InterlockedAdd(0, waveVisible, waveOffset);
    }
    waveOffset = WaveReadLaneFirst(waveOffset);
    uint compactedIndex = waveOffset + WavePrefixCountBits(isVisible);

    if (isVisible)
    {
        // Write preprocessed splat to dense output
        HWRasterSplat result;
        result.centerClip = splatCenterClip;
        result.ndcBasis1  = splatNdcBasis1;
        result.ndcBasis2  = splatNdcBasis2;
        result.color      = splatColor;
        preprocessedSplats[compactedIndex] = result;

        // Write sort key pair: depth key + compacted index
        uint depthKey = FloatToSortableUint(splatDepth, pc.sortAscending);
        sortPairsOutput[compactedIndex] = uint2(depthKey, compactedIndex);
    }

    AllMemoryBarrierWithGroupSync();

    // Thread 0 of each group increments completion counter
    if (gtid == 0)
    {
        atomicCounter.InterlockedAdd(4, 1, s_completedGroups);
    }

    // Last workgroup writes all indirect dispatch/draw commands
    if (gtid == 0 && s_completedGroups == pc.totalWorkgroups - 1)
    {
        uint visibleCount = atomicCounter.Load(0);
        WriteIndirectArgs(visibleCount);
    }
}
