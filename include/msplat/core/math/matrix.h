#pragma once

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
inline mat4 identity()
{
	return mat4(1.0f);
}

// Translation
inline mat4 translate(const mat4 &m, const vec3 &v)
{
	return glm::translate(m, v);
}

inline mat4 translate(const vec3 &v)
{
	return glm::translate(identity(), v);
}

// Rotation
inline mat4 rotate(const mat4 &m, float angle, const vec3 &axis)
{
	return glm::rotate(m, angle, axis);
}

inline mat4 rotate(float angle, const vec3 &axis)
{
	return glm::rotate(identity(), angle, axis);
}

inline mat4 rotateX(float angle)
{
	return rotate(angle, vec3(1.0f, 0.0f, 0.0f));
}

inline mat4 rotateY(float angle)
{
	return rotate(angle, vec3(0.0f, 1.0f, 0.0f));
}

inline mat4 rotateZ(float angle)
{
	return rotate(angle, vec3(0.0f, 0.0f, 1.0f));
}

// Scale
inline mat4 scale(const mat4 &m, const vec3 &v)
{
	return glm::scale(m, v);
}

inline mat4 scale(const vec3 &v)
{
	return glm::scale(identity(), v);
}

inline mat4 scale(float s)
{
	return scale(vec3(s));
}

// Matrix operations
template <typename T>
constexpr T transpose(const T &m)
{
	return glm::transpose(m);
}

template <typename T>
constexpr T inverse(const T &m)
{
	return glm::inverse(m);
}

template <typename T>
constexpr auto determinant(const T &m)
{
	return glm::determinant(m);
}

// Matrix comparison
template <typename T>
constexpr bool matrixEqual(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::all(glm::epsilonEqual(a, b, epsilon));
}

template <typename T>
constexpr bool matrixNotEqual(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::any(glm::epsilonNotEqual(a, b, epsilon));
}

}        // namespace msplat::math