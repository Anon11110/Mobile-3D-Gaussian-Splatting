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
constexpr T Radians(T degrees)
{
	return glm::radians(degrees);
}

template <typename T>
constexpr T Degrees(T radians)
{
	return glm::degrees(radians);
}

template <typename T>
constexpr T Clamp(T value, T min, T max)
{
	return glm::clamp(value, min, max);
}

template <typename T>
constexpr T Lerp(T a, T b, float t)
{
	return glm::mix(a, b, t);
}

template <typename T>
constexpr T Smoothstep(T edge0, T edge1, T x)
{
	return glm::smoothstep(edge0, edge1, x);
}

template <typename T>
constexpr T Sign(T x)
{
	return glm::sign(x);
}

// Common math functions
template <typename T>
constexpr T Sqrt(T x)
{
	return glm::sqrt(x);
}

template <typename T>
constexpr T Min(T a, T b)
{
	return glm::min(a, b);
}

template <typename T>
constexpr T Max(T a, T b)
{
	return glm::max(a, b);
}

template <typename T>
constexpr T Pow(T base, T exponent)
{
	return glm::pow(base, exponent);
}

template <typename T>
constexpr T Exp(T x)
{
	return glm::exp(x);
}

template <typename T>
constexpr T Log(T x)
{
	return glm::log(x);
}

}        // namespace msplat::math