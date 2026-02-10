// This fragment shader colors particles by cycling through the HSV color spectrum
// over time, creating a vibrant rainbow effect. Additive blending should be enabled.

struct PSInput
{
	float4 position : SV_Position;
	nointerpolation float emitterID : TEXCOORD0;
	float3 worldPosition : TEXCOORD1;
};

struct PSOutput
{
	float4 color : SV_Target0;
};

// Uniform buffer containing simulation parameters, including global time.
// This must match the layout of the C++ SimulationParams struct.
struct SimulationParams
{
	float deltaTime;
	float time;
};

[[vk::binding(1, 0)]]
ConstantBuffer<SimulationParams> params : register(b1);

// Converts a color from HSV (Hue, Saturation, Value) to RGB color space.
// All input and output values are in the range [0, 1].
float3 hsv2rgb(float3 c)
{
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

[[vk::ext_builtin_input(/* PointCoord */ 16)]]
static const float2 gl_PointCoord;

PSOutput main(PSInput input)
{
	// Create a circular shape for the particle sprite
	float2 coord = gl_PointCoord - float2(0.5, 0.5);
	float dist = length(coord);

	// Discard fragment if it's outside the circle's radius
	if (dist > 0.5)
	{
		discard;
	}

	// --- COLORING LOGIC ---

	// The base hue cycles over time, creating the main rainbow animation.
	// The time is slowed down to make the color transition smooth and pleasant.
	float baseHue = frac(params.time * 0.1);

	// An offset is added based on the emitter ID (0 or 1).
	// This gives each smoke stream a different starting color in the spectrum.
	float emitterHueOffset = input.emitterID * 0.5;

	// A small offset based on the particle's horizontal position adds more
	// color variation within each individual stream of smoke.
	float positionHueOffset = input.worldPosition.x * 0.05;

	// Combine the hue components. 'frac' wraps the result to stay in the [0, 1] range.
	float finalHue = frac(baseHue + emitterHueOffset + positionHueOffset);

	// Keep saturation and value (brightness) high for vibrant, glowing colors.
	float saturation = 0.9;
	float value      = 1.0;

	// Convert the final calculated HSV color to the RGB color space for display.
	float3 finalColor = hsv2rgb(float3(finalHue, saturation, value));

	// --- ALPHA & GLOW ---

	// Calculate alpha for soft edges, which makes the center of each particle brighter.
	// A low alpha value is used here because it works best with the additive blending
	// enabled in the C++ application to create the glow effect.
	float alpha = (1.0 - smoothstep(0.1, 0.5, dist)) * 0.2;

	PSOutput output;
	output.color = float4(finalColor, alpha);
	return output;
}
