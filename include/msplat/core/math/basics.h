#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace msplat::math
{

// Common mathematical constants
constexpr float PI      = glm::pi<float>();
constexpr float TWO_PI  = glm::two_pi<float>();
constexpr float HALF_PI = glm::half_pi<float>();
constexpr float EPSILON = glm::epsilon<float>();

// Utility functions
template <typename T>
constexpr T radians(T degrees)
{
	return glm::radians(degrees);
}

template <typename T>
constexpr T degrees(T radians)
{
	return glm::degrees(radians);
}

template <typename T>
constexpr T clamp(T value, T min, T max)
{
	return glm::clamp(value, min, max);
}

template <typename T>
constexpr T lerp(T a, T b, float t)
{
	return glm::mix(a, b, t);
}

template <typename T>
constexpr T smoothstep(T edge0, T edge1, T x)
{
	return glm::smoothstep(edge0, edge1, x);
}

template <typename T>
constexpr T sign(T x)
{
	return glm::sign(x);
}

// Common math functions
template <typename T>
constexpr T sqrt(T x)
{
	return glm::sqrt(x);
}

template <typename T>
constexpr T min(T a, T b)
{
	return glm::min(a, b);
}

template <typename T>
constexpr T max(T a, T b)
{
	return glm::max(a, b);
}

template <typename T>
constexpr T pow(T base, T exponent)
{
	return glm::pow(base, exponent);
}

template <typename T>
constexpr T exp(T x)
{
	return glm::exp(x);
}

template <typename T>
constexpr T log(T x)
{
	return glm::log(x);
}

}        // namespace msplat::math