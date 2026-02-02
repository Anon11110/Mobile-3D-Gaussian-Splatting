#include "shaderio.h"

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
[[vk::binding(5, 0)]] StructuredBuffer<uint> indices;
[[vk::binding(6, 0)]] StructuredBuffer<uint> meshIndices;
[[vk::binding(7, 0)]] StructuredBuffer<float4x4> modelMatrices;

struct VSOutput
{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] noperspective float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

// Struct for 2D covariance with alpha compensation
struct Covariance2D
{
    float3 cov2d;      // (a, b, c) - upper triangle of 2x2 matrix
    float  alphaScale; // determinant-based AA compensation
};

float3x3 QuatToMat3(float4 q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return float3x3(
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y),
        2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x),
        2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y)
    );
}

// Kill a splat by making it offscreen
VSOutput KillSplat()
{
    VSOutput output;
    output.position = float4(0.0, 0.0, 2.0, 1.0);
    output.color    = float4(0.0, 0.0, 0.0, 0.0);
    output.uv       = float2(0.0, 0.0);
    return output;
}

// SH evaluation function for degrees 0-3
float3 ComputeSH(uint splatIndex, float3 dir, float3 baseColor)
{
    // The shRest buffer contains 45 floats per splat in planar format:
    // [0-14]: Red channel coefficients for degrees 1-3
    // [15-29]: Green channel coefficients for degrees 1-3
    // [30-44]: Blue channel coefficients for degrees 1-3
    uint offset = splatIndex * 45;

    float x = dir.x, y = dir.y, z = dir.z;

    // Degree 0 (base color)
    float3 result = baseColor;

    // Degree 1 (3 coefficients per channel)
    result.r += SH_C1 * (-y * shRest[offset + 0] + z * shRest[offset + 1] - x * shRest[offset + 2]);
    result.g += SH_C1 * (-y * shRest[offset + 15] + z * shRest[offset + 16] - x * shRest[offset + 17]);
    result.b += SH_C1 * (-y * shRest[offset + 30] + z * shRest[offset + 31] - x * shRest[offset + 32]);

    // Degree 2 (5 coefficients per channel)
    float xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;
    result.r += SH_C2_0 * xy * shRest[offset + 3] +
                SH_C2_1 * yz * shRest[offset + 4] +
                SH_C2_2 * (2.0 * zz - xx - yy) * shRest[offset + 5] +
                SH_C2_3 * xz * shRest[offset + 6] +
                SH_C2_4 * (xx - yy) * shRest[offset + 7];
    result.g += SH_C2_0 * xy * shRest[offset + 18] +
                SH_C2_1 * yz * shRest[offset + 19] +
                SH_C2_2 * (2.0 * zz - xx - yy) * shRest[offset + 20] +
                SH_C2_3 * xz * shRest[offset + 21] +
                SH_C2_4 * (xx - yy) * shRest[offset + 22];
    result.b += SH_C2_0 * xy * shRest[offset + 33] +
                SH_C2_1 * yz * shRest[offset + 34] +
                SH_C2_2 * (2.0 * zz - xx - yy) * shRest[offset + 35] +
                SH_C2_3 * xz * shRest[offset + 36] +
                SH_C2_4 * (xx - yy) * shRest[offset + 37];

    // Degree 3 (7 coefficients per channel)
    result.r += SH_C3_0 * y * (3.0 * xx - yy) * shRest[offset + 8] +
                SH_C3_1 * xy * z * shRest[offset + 9] +
                SH_C3_2 * y * (4.0 * zz - xx - yy) * shRest[offset + 10] +
                SH_C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * shRest[offset + 11] +
                SH_C3_4 * x * (4.0 * zz - xx - yy) * shRest[offset + 12] +
                SH_C3_5 * z * (xx - yy) * shRest[offset + 13] +
                SH_C3_6 * x * (xx - 3.0 * yy) * shRest[offset + 14];
    result.g += SH_C3_0 * y * (3.0 * xx - yy) * shRest[offset + 23] +
                SH_C3_1 * xy * z * shRest[offset + 24] +
                SH_C3_2 * y * (4.0 * zz - xx - yy) * shRest[offset + 25] +
                SH_C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * shRest[offset + 26] +
                SH_C3_4 * x * (4.0 * zz - xx - yy) * shRest[offset + 27] +
                SH_C3_5 * z * (xx - yy) * shRest[offset + 28] +
                SH_C3_6 * x * (xx - 3.0 * yy) * shRest[offset + 29];
    result.b += SH_C3_0 * y * (3.0 * xx - yy) * shRest[offset + 38] +
                SH_C3_1 * xy * z * shRest[offset + 39] +
                SH_C3_2 * y * (4.0 * zz - xx - yy) * shRest[offset + 40] +
                SH_C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * shRest[offset + 41] +
                SH_C3_4 * x * (4.0 * zz - xx - yy) * shRest[offset + 42] +
                SH_C3_5 * z * (xx - yy) * shRest[offset + 43] +
                SH_C3_6 * x * (xx - 3.0 * yy) * shRest[offset + 44];

    return max(result, float3(0.0, 0.0, 0.0));
}

// Helper to fetch pre-computed 3D covariance matrix from buffer
float3x3 GetCovariance3D(uint splatIndex)
{
    // Fetch packed covariance (6 floats stored as 2 float3)
    float3 upper = cov3DPacked[splatIndex * 2];       // M11, M12, M13
    float3 lower = cov3DPacked[splatIndex * 2 + 1];   // M22, M23, M33

    return float3x3(
        upper.x, upper.y, upper.z,   // Row 1: M11, M12, M13
        upper.y, lower.x, lower.y,   // Row 2: M12, M22, M23
        upper.z, lower.y, lower.z    // Row 3: M13, M23, M33
    );
}

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

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;

    // Instanced rendering: instanceId is the splat ID
    // vertexId will be 0, 1, 2, or 3 (from our tiny index buffer)
    uint splatId    = instanceId;
    uint splatIndex = indices[splatId];

    float3 localCenter = positions[splatIndex];
    float4 baseColor   = colors[splatIndex];
    float3x3 cov3D     = GetCovariance3D(splatIndex);

    uint meshIdx       = meshIndices[splatIndex];
    float4x4 modelMat  = modelMatrices[meshIdx];

    // Transform position to world space
    float4 worldPos4 = mul(modelMat, float4(localCenter, 1.0));
    float3 center    = worldPos4.xyz;

    // Transform covariance to world space
    // For model matrix M = T * R * S (translation, rotation, scale):
    // cov' = R * S * cov * S^T * R^T = (RS) * cov * (RS)^T
    // Extract 3x3 rotation+scale part from model matrix
    float3x3 modelRS = float3x3(
        modelMat[0].xyz,
        modelMat[1].xyz,
        modelMat[2].xyz
    );
    cov3D = mul(mul(modelRS, cov3D), transpose(modelRS));

    // Alpha culling
    if (baseColor.a < ubo.alphaCullThreshold)
    {
        return KillSplat();
    }

    // Transform to view space
    float4 viewPos4 = mul(ubo.view, float4(center, 1.0));
    float3 viewPos  = viewPos4.xyz;

    // Depth culling
    if (viewPos.z > -0.1)
    {
        return KillSplat();
    }

    float3 viewDir = normalize(ubo.cameraPos.xyz - center);
    float3 shColor = ComputeSH(splatIndex, viewDir, baseColor.rgb);
    float4 color   = float4(shColor, baseColor.a);

    // Covariance Projection
    Covariance2D cov_out = ProjectCovariance3D(cov3D, viewPos);
    float3 cov2d = cov_out.cov2d;

    // Apply determinant-based alpha compensation
    color.a *= cov_out.alphaScale;

    // Eigendecomposition with Radius Caps
    float2 axis1, axis2;
    float  radius1, radius2;

    if (!GetEllipseBasis(cov2d, axis1, axis2, radius1, radius2))
    {
        return KillSplat();
    }

    // Frustum Culling
    float4 centerClip = mul(ubo.projection, viewPos4);
    if (IsCulledByFrustum(centerClip, radius1, radius2))
    {
        return KillSplat();
    }

    // Vertex Generation
    // Triangle strip "Z" pattern mapping
    float2 cornerUnit;
    switch (vertexId)
    {
        case 0:
            cornerUnit = float2(-1.0, -1.0);  // Bottom-Left
            break;
        case 1:
            cornerUnit = float2(1.0, -1.0);   // Bottom-Right
            break;
        case 2:
            cornerUnit = float2(-1.0, 1.0);   // Top-Left
            break;
        default:  // case 3
            cornerUnit = float2(1.0, 1.0);    // Top-Right
            break;
    }

    // Build basis vectors in screen space (pixels)
    float2 basis1 = radius1 * axis1;
    float2 basis2 = radius2 * axis2;

    float2 screenOffset = cornerUnit.x * basis1 + cornerUnit.y * basis2;

    // Rotate screen offset to match pre-rotated clip space
    float2x2 rotMat = float2x2(ubo.screenRotation.xy, ubo.screenRotation.zw);
    float2 rotatedOffset = mul(rotMat, screenOffset);

    // Convert pixels to NDC with adjustable knobs
    float2 ndcOffset = rotatedOffset * ubo.basisViewport * 2.0 * ubo.inverseFocalAdj;

    // Convert NDC offset to clip-space offset
    float2 offsetClip = ndcOffset * centerClip.w;

    output.position = centerClip + float4(offsetClip, 0.0, 0.0);
    output.color    = color;
    output.uv       = cornerUnit;

    return output;
}
