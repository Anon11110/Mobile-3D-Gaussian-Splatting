#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace core
{
namespace math
{

// Quaternion type
using quat = glm::quat;

// Quaternion construction
inline quat quatIdentity()
{
	return quat(1.0f, 0.0f, 0.0f, 0.0f);
}

inline quat quatFromAxisAngle(const vec3 &axis, float angle)
{
	return glm::angleAxis(angle, axis);
}

inline quat quatFromEuler(float pitch, float yaw, float roll)
{
	return quat(vec3(pitch, yaw, roll));
}

inline quat quatFromEuler(const vec3 &eulerAngles)
{
	return quat(eulerAngles);
}

// Convert to matrix
inline mat3 quatToMat3(const quat &q)
{
	return glm::mat3_cast(q);
}

inline mat4 quatToMat4(const quat &q)
{
	return glm::mat4_cast(q);
}

// Convert from matrix
inline quat matToQuat(const mat3 &m)
{
	return glm::quat_cast(m);
}

inline quat matToQuat(const mat4 &m)
{
	return glm::quat_cast(m);
}

// Quaternion operations
inline quat quatNormalize(const quat &q)
{
	return glm::normalize(q);
}

inline quat quatConjugate(const quat &q)
{
	return glm::conjugate(q);
}

inline quat quatInverse(const quat &q)
{
	return glm::inverse(q);
}

inline float quatLength(const quat &q)
{
	return glm::length(q);
}

inline float quatLength2(const quat &q)
{
	return glm::length2(q);
}

inline float quatDot(const quat &a, const quat &b)
{
	return glm::dot(a, b);
}

// Interpolation
inline quat quatSlerp(const quat &a, const quat &b, float t)
{
	return glm::slerp(a, b, t);
}

inline quat quatLerp(const quat &a, const quat &b, float t)
{
	return glm::lerp(a, b, t);
}

// Rotation
inline vec3 quatRotate(const quat &q, const vec3 &v)
{
	return glm::rotate(q, v);
}

// Euler angles
inline vec3 quatToEuler(const quat &q)
{
	return glm::eulerAngles(q);
}

inline float quatPitch(const quat &q)
{
	return glm::pitch(q);
}

inline float quatYaw(const quat &q)
{
	return glm::yaw(q);
}

inline float quatRoll(const quat &q)
{
	return glm::roll(q);
}

// Look at rotation
inline quat quatLookAt(const vec3 &direction, const vec3 &up)
{
	return glm::quatLookAt(direction, up);
}

// Comparison
inline bool quatEqual(const quat &a, const quat &b, float epsilon = glm::epsilon<float>())
{
	return glm::all(glm::epsilonEqual(a, b, epsilon));
}

inline bool quatNotEqual(const quat &a, const quat &b, float epsilon = glm::epsilon<float>())
{
	return glm::any(glm::epsilonNotEqual(a, b, epsilon));
}

}        // namespace math
}        // namespace core