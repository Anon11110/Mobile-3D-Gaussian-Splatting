#pragma once

#include "matrix.h"
#include "quaternion.h"
#include "vector.h"

namespace core
{
namespace math
{

// Affine transform class
class Transform
{
  public:
	vec3 position{0.0f};
	quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
	vec3 scale{1.0f};

	// Constructors
	Transform() = default;
	Transform(const vec3 &pos) :
	    position(pos)
	{}
	Transform(const vec3 &pos, const quat &rot) :
	    position(pos), rotation(rot)
	{}
	Transform(const vec3 &pos, const quat &rot, const vec3 &scl) :
	    position(pos), rotation(rot), scale(scl)
	{}

	// Create transformation matrix
	mat4 toMatrix() const
	{
		mat4 t = translate(position);
		mat4 r = quatToMat4(rotation);
		mat4 s = math::scale(scale);
		return t * r * s;
	}

	// Create inverse transformation matrix
	mat4 toInverseMatrix() const
	{
		mat4 invS = math::scale(vec3(1.0f) / scale);
		mat4 invR = quatToMat4(quatConjugate(rotation));
		mat4 invT = translate(-position);
		return invS * invR * invT;
	}

	// Transform a point
	vec3 transformPoint(const vec3 &point) const
	{
		return position + quatRotate(rotation, scale * point);
	}

	// Transform a direction (no translation, no scale)
	vec3 transformDirection(const vec3 &direction) const
	{
		return quatRotate(rotation, direction);
	}

	// Transform a normal (inverse transpose)
	vec3 transformNormal(const vec3 &normal) const
	{
		return quatRotate(rotation, normal / scale);
	}

	// Inverse transform operations
	vec3 inverseTransformPoint(const vec3 &point) const
	{
		return (quatRotate(quatConjugate(rotation), point - position)) / scale;
	}

	vec3 inverseTransformDirection(const vec3 &direction) const
	{
		return quatRotate(quatConjugate(rotation), direction);
	}

	// Combine transforms
	Transform operator*(const Transform &other) const
	{
		Transform result;
		result.scale    = scale * other.scale;
		result.rotation = rotation * other.rotation;
		result.position = position + quatRotate(rotation, scale * other.position);
		return result;
	}

	Transform &operator*=(const Transform &other)
	{
		*this = *this * other;
		return *this;
	}

	// Interpolation
	static Transform lerp(const Transform &a, const Transform &b, float t)
	{
		Transform result;
		result.position = math::lerp(a.position, b.position, t);
		result.rotation = quatSlerp(a.rotation, b.rotation, t);
		result.scale    = math::lerp(a.scale, b.scale, t);
		return result;
	}

	// Create from matrix (approximation)
	static Transform fromMatrix(const mat4 &matrix)
	{
		Transform result;

		// Extract translation
		result.position = vec3(matrix[3]);

		// Extract scale
		result.scale.x = core::math::length(vec3(matrix[0]));
		result.scale.y = core::math::length(vec3(matrix[1]));
		result.scale.z = core::math::length(vec3(matrix[2]));

		// Extract rotation (normalize the matrix first)
		mat3 rotMatrix = mat3(
		    vec3(matrix[0]) / result.scale.x,
		    vec3(matrix[1]) / result.scale.y,
		    vec3(matrix[2]) / result.scale.z);
		result.rotation = matToQuat(rotMatrix);

		return result;
	}

	// Identity transform
	static Transform identity()
	{
		return Transform();
	}
};

// Create common transforms
inline Transform makeTranslation(const vec3 &translation)
{
	return Transform(translation);
}

inline Transform makeRotation(const quat &rotation)
{
	return Transform(vec3(0.0f), rotation);
}

inline Transform makeScale(const vec3 &scale)
{
	Transform t;
	t.scale = scale;
	return t;
}

inline Transform makeScale(float scale)
{
	return makeScale(vec3(scale));
}

}        // namespace math
}        // namespace core