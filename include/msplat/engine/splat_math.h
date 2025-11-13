#pragma once

#include <msplat/core/math/math.h>

namespace msplat::engine
{

// Spherical harmonics constant for degree 0: 1/(2*sqrt(pi))
constexpr float SH_C0 = 0.28209479177387814f;

/// Compute 3D covariance from scale and rotation (quaternion)
/// Returns 6 unique elements of symmetric 3x3 matrix: [M11, M12, M13, M22, M23, M33]
/// Computes: Cov = R * S * S^T * R^T
inline void ComputeCovariance3D(const math::vec3 &scale, const math::vec4 &rotation,
                                float outCov[6])
{
	// Build rotation matrix from quaternion
	float x = rotation.x, y = rotation.y, z = rotation.z, w = rotation.w;

	// Rotation matrix R (row-major)
	float R[3][3] = {
	    {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - w * z), 2.0f * (x * z + w * y)},
	    {2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - w * x)},
	    {2.0f * (x * z - w * y), 2.0f * (y * z + w * x), 1.0f - 2.0f * (x * x + y * y)}};

	// Scale matrix S (diagonal)
	float sx = scale.x, sy = scale.y, sz = scale.z;

	// Compute M = R * S
	float M[3][3] = {{R[0][0] * sx, R[0][1] * sy, R[0][2] * sz},
	                 {R[1][0] * sx, R[1][1] * sy, R[1][2] * sz},
	                 {R[2][0] * sx, R[2][1] * sy, R[2][2] * sz}};

	// Compute Cov = M * M^T (symmetric, store upper triangle + diagonal)
	outCov[0] = M[0][0] * M[0][0] + M[0][1] * M[0][1] + M[0][2] * M[0][2];        // M11
	outCov[1] = M[0][0] * M[1][0] + M[0][1] * M[1][1] + M[0][2] * M[1][2];        // M12
	outCov[2] = M[0][0] * M[2][0] + M[0][1] * M[2][1] + M[0][2] * M[2][2];        // M13
	outCov[3] = M[1][0] * M[1][0] + M[1][1] * M[1][1] + M[1][2] * M[1][2];        // M22
	outCov[4] = M[1][0] * M[2][0] + M[1][1] * M[2][1] + M[1][2] * M[2][2];        // M23
	outCov[5] = M[2][0] * M[2][0] + M[2][1] * M[2][1] + M[2][2] * M[2][2];        // M33
}

/// Convert log-scale to actual scale using exp()
inline math::vec3 TransformScale(const math::vec3 &log_scale)
{
	return math::vec3(math::Exp(log_scale.x), math::Exp(log_scale.y), math::Exp(log_scale.z));
}

/// Convert opacity logit to alpha [0,1] using sigmoid: 1/(1 + exp(-x))
inline float TransformOpacity(float logit)
{
	return math::Clamp(1.0f / (1.0f + math::Exp(-logit)), 0.0f, 1.0f);
}

/// Convert SH degree 0 coefficients to RGB color [0,1]
inline math::vec3 ComputeSHDegree0Color(float dc0, float dc1, float dc2)
{
	return math::vec3(
	    math::Clamp(SH_C0 * dc0 + 0.5f, 0.0f, 1.0f),
	    math::Clamp(SH_C0 * dc1 + 0.5f, 0.0f, 1.0f),
	    math::Clamp(SH_C0 * dc2 + 0.5f, 0.0f, 1.0f));
}

/// Compute view-space depth from world position and view matrix
/// Extracts Z component: view_matrix[*][2] dot position
inline float ComputeViewSpaceDepth(const math::vec3 &world_pos, const math::mat4 &view_matrix)
{
	return view_matrix[0][2] * world_pos.x +
	       view_matrix[1][2] * world_pos.y +
	       view_matrix[2][2] * world_pos.z;
}

}        // namespace msplat::engine
