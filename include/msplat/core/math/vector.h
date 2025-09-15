#pragma once

#include <glm/ext/scalar_constants.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

namespace msplat::math
{

// 2D vector types
using vec2  = glm::vec2;
using ivec2 = glm::ivec2;
using uvec2 = glm::uvec2;

// 3D vector types
using vec3  = glm::vec3;
using ivec3 = glm::ivec3;
using uvec3 = glm::uvec3;

// 4D vector types
using vec4  = glm::vec4;
using ivec4 = glm::ivec4;
using uvec4 = glm::uvec4;

// Vector operations
template <typename T>
inline auto Dot(const T &a, const T &b)
{
	return glm::dot(a, b);
}

inline vec3 Cross(const vec3 &a, const vec3 &b)
{
	return glm::cross(a, b);
}

template <typename T>
inline auto Length(const T &v)
{
	return glm::length(v);
}

template <typename T>
inline auto Length2(const T &v)
{
	return Dot(v, v);
}

template <typename T>
inline T Normalize(const T &v)
{
	return glm::normalize(v);
}

template <typename T>
inline auto Distance(const T &a, const T &b)
{
	return glm::distance(a, b);
}

template <typename T>
inline auto Distance2(const T &a, const T &b)
{
	return Length2(a - b);
}

template <typename T>
constexpr T Reflect(const T &I, const T &N)
{
	return glm::reflect(I, N);
}

template <typename T>
constexpr T Refract(const T &I, const T &N, float eta)
{
	return glm::refract(I, N, eta);
}

// Component-wise operations
template <typename T>
constexpr T Min(const T &a, const T &b)
{
	return glm::min(a, b);
}

template <typename T>
constexpr T Max(const T &a, const T &b)
{
	return glm::max(a, b);
}

template <typename T>
constexpr T Abs(const T &v)
{
	return glm::abs(v);
}

// Comparison with epsilon
template <typename T>
constexpr bool Equal(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::all(glm::epsilonEqual(a, b, epsilon));
}

template <typename T>
constexpr bool NotEqual(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::any(glm::epsilonNotEqual(a, b, epsilon));
}

}        // namespace msplat::math