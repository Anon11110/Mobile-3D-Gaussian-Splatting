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

// Struct for 2D covariance with alpha compensation
struct Covariance2D
{
	vec3  cov2d;      // (a, b, c) - upper triangle of 2x2 matrix
	float alphaScale; // determinant-based AA compensation
};

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

/// Projects the 3D covariance to 2D with Mip-Splat AA support
Covariance2D ProjectCovariance3D(mat3 cov3D, vec3 viewPos)
{
	// Projective Jacobian
	Covariance2D result;

	float rz  = 1.0 / viewPos.z;
	float rz2 = rz * rz;
	float fx  = ubo.focal.x;
	float fy  = ubo.focal.y;

	mat3 J = mat3(
		fx * rz, 0.0,     -(fx * viewPos.x) * rz2,
		0.0,     fy * rz, -(fy * viewPos.y) * rz2,
		0.0,     0.0,     0.0
	);

	// Transform 3D covariance to 2D
	mat3 W      = mat3(ubo.view);
	mat3 T      = W * cov3D * transpose(W);
	mat3 cov2Dm = J * T * transpose(J);

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

	result.cov2d      = vec3(a_blur, b_blur, c_blur);
	result.alphaScale = alphaScale;
	return result;
}

/// Calculates the basis vectors and radii for the 2D ellipse with radius caps
bool GetEllipseBasis(vec3 cov2d, out vec2 axis1, out vec2 axis2, out float radius1, out float radius2)
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
		return false;
	}

	// Eigendecomposition
	float mid          = 0.5 * (a + c);
	float discriminant = max(0.1, mid * mid - det);
	float lambda1      = mid + sqrt(discriminant);
	float lambda2      = mid - sqrt(discriminant);

	// Check for eigenvalue degeneracy (degenerate projection to line or point)
	if (lambda2 <= 0.0)
	{
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
	// Instanced rendering: gl_InstanceIndex is the splat ID
	// gl_VertexIndex will be 0, 1, 2, or 3 (from our tiny index buffer)
	uint splatId    = gl_InstanceIndex;
	uint vertexId   = gl_VertexIndex;
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
	Covariance2D cov_out = ProjectCovariance3D(cov3D, viewPos);
	vec3 cov2d = cov_out.cov2d;

	// Apply determinant-based alpha compensation
	color.a *= cov_out.alphaScale;

	// --- 5. Eigendecomposition with Radius Caps ---
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
	// Triangle strip "Z" pattern mapping:
	// This forms two triangles: (0,1,2) and (1,2,3)
	vec2 cornerUnit;
	switch (vertexId)
	{
		case 0:
			cornerUnit = vec2(-1.0, -1.0);  // Bottom-Left
			break;
		case 1:
			cornerUnit = vec2(1.0, -1.0);   // Bottom-Right
			break;
		case 2:
			cornerUnit = vec2(-1.0, 1.0);   // Top-Left
			break;
		default:  // case 3
			cornerUnit = vec2(1.0, 1.0);    // Top-Right
			break;
	}

	// Build basis vectors in screen space (pixels)
	vec2 basis1 = radius1 * axis1;
	vec2 basis2 = radius2 * axis2;

	vec2 screenOffset = cornerUnit.x * basis1 + cornerUnit.y * basis2;

	// Convert pixels to NDC with adjustable knobs
	vec2 ndcOffset = screenOffset * ubo.basisViewport * 2.0 * ubo.inverseFocalAdj;

	// Convert NDC offset to clip-space offset
	vec2 offsetClip = ndcOffset * centerClip.w;

	gl_Position = centerClip + vec4(offsetClip, 0.0, 0.0);
	outColor    = color;
	outUV       = cornerUnit;
}
