#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "shaderio.h"

// SH Constants for degrees 1-3
const float SH_C1   = 0.4886025119029199;
const float SH_C2_0 = 1.0925484305920792;
const float SH_C2_1 = -1.0925484305920792;
const float SH_C2_2 = 0.31539156525252005;
const float SH_C2_3 = -1.0925484305920792;
const float SH_C2_4 = 0.5462742152960396;
const float SH_C3_0 = -0.5900435899266435;
const float SH_C3_1 = 2.890611442640554;
const float SH_C3_2 = -0.4570457994644658;
const float SH_C3_3 = 0.3731763325901154;
const float SH_C3_4 = -0.4570457994644658;
const float SH_C3_5 = 1.445305721320277;
const float SH_C3_6 = -0.5900435899266435;

// clang-format off

layout(location = 0) out vec4 outColor;
layout(location = 1) noperspective out vec2 outUV;

layout(set = 0, binding = 0)         uniform FrameUBOBlock{ FRAMEUBO_FIELDS } ubo;
layout(set = 0, binding = 1, std430) readonly buffer PositionsBuffer{ vec3 positions[]; };
layout(set = 0, binding = 2, std430) readonly buffer Covariances3DBuffer{ vec3 cov3DPacked[]; };
layout(set = 0, binding = 3, std430) readonly buffer ColorsBuffer{ vec4 colors[]; };
layout(set = 0, binding = 4, std430) readonly buffer SHRestBuffer{ float shRest[]; };
layout(set = 0, binding = 5, std430) readonly buffer SortedIndicesBuffer{ uint indices[]; };

// clang-format on

mat3 QuatToMat3(vec4 q)
{
	float x = q.x, y = q.y, z = q.z, w = q.w;
	return mat3(
	    1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - w * z), 2.0 * (x * z + w * y),
	    2.0 * (x * y + w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - w * x),
	    2.0 * (x * z - w * y), 2.0 * (y * z + w * x), 1.0 - 2.0 * (x * x + y * y));
}

/// Kills the splat
void KillSplat()
{
	gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
	outColor    = vec4(0.0);
	outUV       = vec2(0.0);
}

/// SH evaluation function for degrees 0-3
vec3 ComputeSH(uint splatIndex, vec3 dir, vec3 baseColor)
{
	// The shRest buffer contains 45 floats per splat in planar format:
	// [0-14]: Red channel coefficients for degrees 1-3
	// [15-29]: Green channel coefficients for degrees 1-3
	// [30-44]: Blue channel coefficients for degrees 1-3
	uint offset = splatIndex * 45;

	float x = dir.x, y = dir.y, z = dir.z;

	// Degree 0 (base color)
	vec3 result = baseColor;

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

	return max(result, vec3(0.0));
}

/// Helper to fetch pre-computed 3D covariance matrix from buffer
mat3 GetCovariance3D(uint splatIndex)
{
	// Fetch packed covariance (6 floats stored as 2 vec3)
	vec3 upper = cov3DPacked[splatIndex * 2];            // M11, M12, M13
	vec3 lower = cov3DPacked[splatIndex * 2 + 1];        // M22, M23, M33

	// Reconstruct symmetric matrix
	return mat3(
	    upper.x, upper.y, upper.z,        // Row 1: M11, M12, M13
	    upper.y, lower.x, lower.y,        // Row 2: M12, M22, M23
	    upper.z, lower.y, lower.z         // Row 3: M13, M23, M33
	);
}

/// Projects the 3D covariance to 2D using the Jacobian
vec3 ProjectCovariance(mat3 cov3D, vec3 viewPos)
{
	// Projective Jacobian
	float rz  = 1.0 / viewPos.z;
	float rz2 = rz * rz;
	float fx  = ubo.focal.x;
	float fy  = ubo.focal.y;

	mat3 J = mat3(
	    fx * rz, 0.0, -(fx * viewPos.x) * rz2,
	    0.0, fy * rz, -(fy * viewPos.y) * rz2,
	    0.0, 0.0, 0.0);

	// Transform 3D covariance to 2D
	mat3 W      = mat3(ubo.view);
	mat3 T      = W * cov3D * transpose(W);
	mat3 cov2Dm = J * T * transpose(J);

	// Low-pass filter for anti-aliasing
	cov2Dm[0][0] += 0.3;
	cov2Dm[1][1] += 0.3;

	return vec3(cov2Dm[0][0], cov2Dm[0][1], cov2Dm[1][1]);
}

/// Calculates the basis vectors and radii for the 2D ellipse
bool GetEllipseBasis(vec3 cov2d, out vec2 axis1, out vec2 axis2, out float radius1, out float radius2)
{
	float det = cov2d.x * cov2d.z - cov2d.y * cov2d.y;

	if (det <= 1e-6)
	{
		// Add a small epsilon to the diagonal to make it invertible
		cov2d.x += 1e-6;
		cov2d.z += 1e-6;
		det = cov2d.x * cov2d.z - cov2d.y * cov2d.y;
	}
	if (det <= 0.0)
		return false;

	// Eigendecomposition
	float mid          = 0.5 * (cov2d.x + cov2d.z);
	float discriminant = max(0.0, mid * mid - det);
	float lambda1      = mid + sqrt(discriminant);
	float lambda2      = mid - sqrt(discriminant);

	// Check for eigenvalue degeneracy (degenerate projection to line or point)
	if (lambda2 <= 0.0)
	{
		return false;
	}

	lambda1 = max(lambda1, 1e-8);
	lambda2 = max(lambda2, 1e-8);
	radius1 = sqrt(8.0) * sqrt(lambda1);
	radius2 = sqrt(8.0) * sqrt(lambda2);

	float minPix = 0.5;
	radius1      = max(radius1, minPix);
	radius2      = max(radius2, minPix);

	// Apply splat scale factor
	radius1 *= ubo.splatScale;
	radius2 *= ubo.splatScale;

	// Vector-based eigendecomposition to find axes
	float a = cov2d.x, b = cov2d.y, c = cov2d.z;
	float two_b = 2.0 * b;
	float diff  = a - c;
	float r     = length(vec2(two_b, diff));
	r           = max(r, 1e-20);

	float cosT = sqrt(max(0.0, (r + diff) / (2.0 * r)));
	float sinT = sqrt(max(0.0, (r - diff) / (2.0 * r)));
	sinT       = (b >= 0.0) ? sinT : -sinT;

	axis1 = vec2(cosT, sinT);
	axis2 = vec2(-axis1.y, axis1.x);

	return true;
}

/// Checks if the splat is outside the view frustum
bool IsCulledByFrustum(vec4 centerClip, float radius1, float radius2)
{
	vec2  centerNDC = centerClip.xy / centerClip.w;
	float maxRadius = max(radius1, radius2);
	vec2  ndcMargin = (maxRadius / ubo.viewport) * 2.0;

	return any(lessThan(centerNDC, vec2(-1.3) - ndcMargin)) ||
	       any(greaterThan(centerNDC, vec2(1.3) + ndcMargin));
}

void main()
{
	uint splatId    = gl_VertexIndex / 4;
	uint vertexId   = gl_VertexIndex % 4;
	uint splatIndex = indices[splatId];

	// --- 1. Fetch Splat Data ---
	vec3 center    = positions[splatIndex];
	vec4 baseColor = colors[splatIndex];
	mat3 cov3D     = GetCovariance3D(splatIndex);

	// --- 2. Early Culling ---
	// Alpha culling
	if (baseColor.a < ubo.alphaCullThreshold)
	{
		KillSplat();
		return;
	}

	// Transform to view space
	vec4 viewPos4 = ubo.view * vec4(center, 1.0);
	vec3 viewPos  = viewPos4.xyz;

	// Depth culling
	if (viewPos.z > -0.1)
	{
		KillSplat();
		return;
	}

	// --- 3. Spherical Harmonics ---
	vec3 viewDir = normalize(ubo.cameraPos.xyz - center);
	vec3 shColor = ComputeSH(splatIndex, viewDir, baseColor.rgb);
	vec4 color   = vec4(shColor, baseColor.a);

	// --- 4. Covariance Projection ---
	vec3 cov2d = ProjectCovariance(cov3D, viewPos);

	// --- 5. Eigendecomposition (Compute Splat Basis) ---
	vec2  axis1, axis2;
	float radius1, radius2;

	if (!GetEllipseBasis(cov2d, axis1, axis2, radius1, radius2))
	{
		KillSplat();
		return;
	}

	// --- 6. Frustum Culling ---
	vec4 centerClip = ubo.projection * viewPos4;
	if (IsCulledByFrustum(centerClip, radius1, radius2))
	{
		KillSplat();
		return;
	}

	// --- 7. Vertex Generation ---
	vec2 cornerUnit;
	switch (vertexId)
	{
		case 0:
			cornerUnit = vec2(-1.0, -1.0);
			break;
		case 1:
			cornerUnit = vec2(1.0, -1.0);
			break;
		case 2:
			cornerUnit = vec2(1.0, 1.0);
			break;
		case 3:
			cornerUnit = vec2(-1.0, 1.0);
			break;
	}

	vec2 screenOffset = cornerUnit.x * radius1 * axis1 + cornerUnit.y * radius2 * axis2;
	vec2 offsetClip   = (screenOffset / ubo.viewport) * 2.0 * centerClip.w;

	gl_Position = centerClip + vec4(offsetClip, 0.0, 0.0);
	outColor    = color;
	outUV       = cornerUnit;
}
