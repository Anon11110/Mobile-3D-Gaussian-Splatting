#pragma once

#include "basics.h"
#include "vector.h"
#include <algorithm>
#include <cmath>

namespace core
{
namespace math
{

// Color types
using Color3 = vec3;        // RGB
using Color4 = vec4;        // RGBA

// Common colors (RGB)
namespace Colors
{
constexpr Color3 BLACK{0.0f, 0.0f, 0.0f};
constexpr Color3 WHITE{1.0f, 1.0f, 1.0f};
constexpr Color3 RED{1.0f, 0.0f, 0.0f};
constexpr Color3 GREEN{0.0f, 1.0f, 0.0f};
constexpr Color3 BLUE{0.0f, 0.0f, 1.0f};
constexpr Color3 YELLOW{1.0f, 1.0f, 0.0f};
constexpr Color3 CYAN{0.0f, 1.0f, 1.0f};
constexpr Color3 MAGENTA{1.0f, 0.0f, 1.0f};
constexpr Color3 GRAY{0.5f, 0.5f, 0.5f};
constexpr Color3 ORANGE{1.0f, 0.5f, 0.0f};
constexpr Color3 PURPLE{0.5f, 0.0f, 1.0f};
}        // namespace Colors

// Color conversion functions
inline Color3 hsvToRgb(float h, float s, float v)
{
	float c = v * s;
	float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;

	Color3 rgb;
	if (h >= 0.0f && h < 60.0f)
	{
		rgb = Color3(c, x, 0.0f);
	}
	else if (h >= 60.0f && h < 120.0f)
	{
		rgb = Color3(x, c, 0.0f);
	}
	else if (h >= 120.0f && h < 180.0f)
	{
		rgb = Color3(0.0f, c, x);
	}
	else if (h >= 180.0f && h < 240.0f)
	{
		rgb = Color3(0.0f, x, c);
	}
	else if (h >= 240.0f && h < 300.0f)
	{
		rgb = Color3(x, 0.0f, c);
	}
	else
	{
		rgb = Color3(c, 0.0f, x);
	}

	return rgb + Color3(m);
}

inline Color3 hsvToRgb(const vec3 &hsv)
{
	return hsvToRgb(hsv.x, hsv.y, hsv.z);
}

inline vec3 rgbToHsv(const Color3 &rgb)
{
	float maxVal = std::max({rgb.r, rgb.g, rgb.b});
	float minVal = std::min({rgb.r, rgb.g, rgb.b});
	float delta  = maxVal - minVal;

	vec3 hsv;
	hsv.z = maxVal;        // Value

	if (maxVal == 0.0f)
	{
		hsv.y = 0.0f;        // Saturation
		hsv.x = 0.0f;        // Hue
	}
	else
	{
		hsv.y = delta / maxVal;        // Saturation

		if (delta == 0.0f)
		{
			hsv.x = 0.0f;        // Hue
		}
		else if (maxVal == rgb.r)
		{
			hsv.x = 60.0f * std::fmod((rgb.g - rgb.b) / delta, 6.0f);
		}
		else if (maxVal == rgb.g)
		{
			hsv.x = 60.0f * ((rgb.b - rgb.r) / delta + 2.0f);
		}
		else
		{
			hsv.x = 60.0f * ((rgb.r - rgb.g) / delta + 4.0f);
		}

		if (hsv.x < 0.0f)
		{
			hsv.x += 360.0f;
		}
	}

	return hsv;
}

// Gamma correction
inline Color3 linearToSrgb(const Color3 &linear)
{
	Color3 srgb;
	for (int i = 0; i < 3; ++i)
	{
		if (linear[i] <= 0.0031308f)
		{
			srgb[i] = 12.92f * linear[i];
		}
		else
		{
			srgb[i] = 1.055f * std::pow(linear[i], 1.0f / 2.4f) - 0.055f;
		}
	}
	return srgb;
}

inline Color3 srgbToLinear(const Color3 &srgb)
{
	Color3 linear;
	for (int i = 0; i < 3; ++i)
	{
		if (srgb[i] <= 0.04045f)
		{
			linear[i] = srgb[i] / 12.92f;
		}
		else
		{
			linear[i] = std::pow((srgb[i] + 0.055f) / 1.055f, 2.4f);
		}
	}
	return linear;
}

// Simple gamma correction
inline Color3 gammaCorrect(const Color3 &color, float gamma = 2.2f)
{
	return Color3(
	    std::pow(color.r, 1.0f / gamma),
	    std::pow(color.g, 1.0f / gamma),
	    std::pow(color.b, 1.0f / gamma));
}

inline Color3 inverseGammaCorrect(const Color3 &color, float gamma = 2.2f)
{
	return Color3(
	    std::pow(color.r, gamma),
	    std::pow(color.g, gamma),
	    std::pow(color.b, gamma));
}

// Color blending
inline Color4 alphaBlend(const Color4 &source, const Color4 &dest)
{
	float srcAlpha    = source.a;
	float invSrcAlpha = 1.0f - srcAlpha;

	return Color4(
	    source.r * srcAlpha + dest.r * invSrcAlpha,
	    source.g * srcAlpha + dest.g * invSrcAlpha,
	    source.b * srcAlpha + dest.b * invSrcAlpha,
	    source.a + dest.a * invSrcAlpha);
}

// Color temperature to RGB (approximate)
inline Color3 temperatureToRgb(float temperature)
{
	// Temperature in Kelvin, typically 1000-12000
	temperature = clamp(temperature, 1000.0f, 12000.0f) / 100.0f;

	Color3 rgb;

	// Red
	if (temperature <= 66.0f)
	{
		rgb.r = 1.0f;
	}
	else
	{
		rgb.r = 1.292936f * std::pow(temperature - 60.0f, -0.1332047f);
		rgb.r = clamp(rgb.r, 0.0f, 1.0f);
	}

	// Green
	if (temperature <= 66.0f)
	{
		rgb.g = 0.39008157f * std::log(temperature) - 0.63184144f;
	}
	else
	{
		rgb.g = 1.292936f * std::pow(temperature - 60.0f, -0.0755148f);
	}
	rgb.g = clamp(rgb.g, 0.0f, 1.0f);

	// Blue
	if (temperature >= 66.0f)
	{
		rgb.b = 1.0f;
	}
	else if (temperature <= 19.0f)
	{
		rgb.b = 0.0f;
	}
	else
	{
		rgb.b = 0.543206789f * std::log(temperature - 10.0f) - 1.19625408f;
		rgb.b = clamp(rgb.b, 0.0f, 1.0f);
	}

	return rgb;
}

// Luminance calculation
inline float luminance(const Color3 &color)
{
	return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
}

// Color distance
inline float colorDistance(const Color3 &a, const Color3 &b)
{
	return length(a - b);
}

// Premultiplied alpha operations
inline Color4 premultiplyAlpha(const Color4 &color)
{
	return Color4(vec3(color) * color.a, color.a);
}

inline Color4 unpremultiplyAlpha(const Color4 &color)
{
	if (color.a == 0.0f)
		return Color4(0.0f);
	return Color4(vec3(color) / color.a, color.a);
}

}        // namespace math
}        // namespace core