#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace msplat::math
{

// Quaternion type
using quat = glm::quat;

// Quaternion construction
inline quat QuatIdentity()
{
	return quat(1.0f, 0.0f, 0.0f, 0.0f);
}

inline quat QuatFromAxisAngle(const vec3 &axis, float angle)
{
	return glm::angleAxis(angle, axis);
}

inline quat QuatFromEuler(float pitch, float yaw, float roll)
{
	return quat(vec3(pitch, yaw, roll));
}

inline quat QuatFromEuler(const vec3 &eulerAngles)
{
	return quat(eulerAngles);
}

// Convert to matrix
inline mat3 QuatToMat3(const quat &q)
{
	return glm::mat3_cast(q);
}

inline mat4 QuatToMat4(const quat &q)
{
	return glm::mat4_cast(q);
}

// Convert from matrix
inline quat MatToQuat(const mat3 &m)
{
	return glm::quat_cast(m);
}

inline quat MatToQuat(const mat4 &m)
{
	return glm::quat_cast(m);
}

// Quaternion operations
inline quat QuatNormalize(const quat &q)
{
	return glm::normalize(q);
}

inline quat QuatConjugate(const quat &q)
{
	return glm::conjugate(q);
}

inline quat QuatInverse(const quat &q)
{
	return glm::inverse(q);
}

inline float QuatLength(const quat &q)
{
	return glm::length(q);
}

inline float QuatLength2(const quat &q)
{
	return glm::length2(q);
}

inline float QuatDot(const quat &a, const quat &b)
{
	return glm::dot(a, b);
}

// Interpolation
inline quat QuatSlerp(const quat &a, const quat &b, float t)
{
	return glm::slerp(a, b, t);
}

inline quat QuatLerp(const quat &a, const quat &b, float t)
{
	return glm::lerp(a, b, t);
}

// Rotation
inline vec3 QuatRotate(const quat &q, const vec3 &v)
{
	return glm::rotate(q, v);
}

// Euler angles
inline vec3 QuatToEuler(const quat &q)
{
	return glm::eulerAngles(q);
}

inline float QuatPitch(const quat &q)
{
	return glm::pitch(q);
}

inline float QuatYaw(const quat &q)
{
	return glm::yaw(q);
}

inline float QuatRoll(const quat &q)
{
	return glm::roll(q);
}

// Look at rotation
inline quat QuatLookAt(const vec3 &direction, const vec3 &up)
{
	return glm::quatLookAt(direction, up);
}

// Comparison
inline bool QuatEqual(const quat &a, const quat &b, float epsilon = glm::epsilon<float>())
{
	return glm::all(glm::epsilonEqual(a, b, epsilon));
}

inline bool QuatNotEqual(const quat &a, const quat &b, float epsilon = glm::epsilon<float>())
{
	return glm::any(glm::epsilonNotEqual(a, b, epsilon));
}

}        // namespace msplat::math