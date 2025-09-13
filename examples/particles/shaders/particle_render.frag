#version 450

// This fragment shader colors particles by cycling through the HSV color spectrum
// over time, creating a vibrant rainbow effect. Additive blending should be enabled.

layout(location = 0) in flat float fragEmitterID;
layout(location = 1) in vec3 fragPosition;

layout(location = 0) out vec4 outColor;

// Uniform buffer containing simulation parameters, including global time.
// This must match the layout of the C++ SimulationParams struct.
layout(set = 0, binding = 1) uniform SimulationParams
{
	float deltaTime;
	float time;
}
params;

// Converts a color from HSV (Hue, Saturation, Value) to RGB color space.
// All input and output values are in the range [0, 1].
vec3 hsv2rgb(vec3 c)
{
	vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main()
{
	// Create a circular shape for the particle sprite
	vec2  coord = gl_PointCoord - vec2(0.5);
	float dist  = length(coord);

	// Discard fragment if it's outside the circle's radius
	if (dist > 0.5)
	{
		discard;
	}

	// --- COLORING LOGIC ---

	// The base hue cycles over time, creating the main rainbow animation.
	// The time is slowed down to make the color transition smooth and pleasant.
	float baseHue = fract(params.time * 0.1);

	// An offset is added based on the emitter ID (0 or 1).
	// This gives each smoke stream a different starting color in the spectrum.
	float emitterHueOffset = fragEmitterID * 0.5;

	// A small offset based on the particle's horizontal position adds more
	// color variation within each individual stream of smoke.
	float positionHueOffset = fragPosition.x * 0.05;

	// Combine the hue components. 'fract' wraps the result to stay in the [0, 1] range.
	float finalHue = fract(baseHue + emitterHueOffset + positionHueOffset);

	// Keep saturation and value (brightness) high for vibrant, glowing colors.
	float saturation = 0.9;
	float value      = 1.0;

	// Convert the final calculated HSV color to the RGB color space for display.
	vec3 finalColor = hsv2rgb(vec3(finalHue, saturation, value));

	// --- ALPHA & GLOW ---

	// Calculate alpha for soft edges, which makes the center of each particle brighter.
	// A low alpha value is used here because it works best with the additive blending
	// enabled in the C++ application to create the glow effect.
	float alpha = (1.0 - smoothstep(0.1, 0.5, dist)) * 0.2;

	outColor = vec4(finalColor, alpha);
}
