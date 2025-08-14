#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

namespace core
{
namespace math
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
inline auto dot(const T &a, const T &b)
{
	return glm::dot(a, b);
}

inline vec3 cross(const vec3 &a, const vec3 &b)
{
	return glm::cross(a, b);
}

template <typename T>
inline auto length(const T &v)
{
	return glm::length(v);
}

template <typename T>
inline auto length2(const T &v)
{
	return dot(v, v);
}

template <typename T>
inline T normalize(const T &v)
{
	return glm::normalize(v);
}

template <typename T>
inline auto distance(const T &a, const T &b)
{
	return glm::distance(a, b);
}

template <typename T>
inline auto distance2(const T &a, const T &b)
{
	return length2(a - b);
}

template <typename T>
constexpr T reflect(const T &I, const T &N)
{
	return glm::reflect(I, N);
}

template <typename T>
constexpr T refract(const T &I, const T &N, float eta)
{
	return glm::refract(I, N, eta);
}

// Component-wise operations
template <typename T>
constexpr T min(const T &a, const T &b)
{
	return glm::min(a, b);
}

template <typename T>
constexpr T max(const T &a, const T &b)
{
	return glm::max(a, b);
}

template <typename T>
constexpr T abs(const T &v)
{
	return glm::abs(v);
}

// Comparison with epsilon
template <typename T>
constexpr bool equal(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::all(glm::epsilonEqual(a, b, epsilon));
}

template <typename T>
constexpr bool notEqual(const T &a, const T &b, float epsilon = glm::epsilon<float>())
{
	return glm::any(glm::epsilonNotEqual(a, b, epsilon));
}

}        // namespace math
}        // namespace core