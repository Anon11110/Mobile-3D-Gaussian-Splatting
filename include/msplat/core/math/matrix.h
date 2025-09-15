#pragma once

#include "vector.h"
#include <glm/ext/scalar_constants.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace msplat::math
{

// Matrix types
using mat2 = glm::mat2;
using mat3 = glm::mat3;
using mat4 = glm::mat4;

// Matrix construction
inline mat4 Identity()
{
	return mat4(1.0f);
}

// Translation
inline mat4 Translate(const mat4 &m, const vec3 &v)
{
	return glm::translate(m, v);
}

inline mat4 Translate(const vec3 &v)
{
	return glm::translate(Identity(), v);
}

// Rotation
inline mat4 Rotate(const mat4 &m, float angle, const vec3 &axis)
{
	return glm::rotate(m, angle, axis);
}

inline mat4 Rotate(float angle, const vec3 &axis)
{
	return glm::rotate(Identity(), angle, axis);
}

inline mat4 RotateX(float angle)
{
	return Rotate(angle, vec3(1.0f, 0.0f, 0.0f));
}

inline mat4 RotateY(float angle)
{
	return Rotate(angle, vec3(0.0f, 1.0f, 0.0f));
}

inline mat4 RotateZ(float angle)
{
	return Rotate(angle, vec3(0.0f, 0.0f, 1.0f));
}

// Scale
inline mat4 Scale(const mat4 &m, const vec3 &v)
{
	return glm::scale(m, v);
}

inline mat4 Scale(const vec3 &v)
{
	return glm::scale(Identity(), v);
}

inline mat4 Scale(float s)
{
	return Scale(vec3(s));
}

// Matrix operations
template <typename T>
constexpr T Transpose(const T &m)
{
	return glm::transpose(m);
}

template <typename T>
constexpr T Inverse(const T &m)
{
	return glm::inverse(m);
}

template <typename T>
constexpr auto Determinant(const T &m)
{
	return glm::determinant(m);
}

// Matrix comparison
template <typename T>
constexpr bool MatrixEqual(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::all(glm::epsilonEqual(a, b, epsilon));
}

template <typename T>
constexpr bool MatrixNotEqual(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::any(glm::epsilonNotEqual(a, b, epsilon));
}

}        // namespace msplat::math