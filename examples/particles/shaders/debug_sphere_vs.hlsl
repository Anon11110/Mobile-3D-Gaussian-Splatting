// Simple vertex shader for drawing a wireframe sphere.
// It transforms the sphere's vertices using a Model-View-Projection matrix.

struct VSInput
{
	float3 position : TEXCOORD0;
};

struct VSOutput
{
	float4 position : SV_Position;
};

// Uniform Buffer Object containing the combined MVP matrix
// for the specific sphere instance being drawn.
struct DebugUBO
{
	float4x4 mvp;
};

[[vk::binding(0, 0)]]
ConstantBuffer<DebugUBO> ubo : register(b0);

VSOutput main(VSInput input)
{
	VSOutput output;
	// Transform the vertex position by the MVP matrix.
	output.position = mul(ubo.mvp, float4(input.position, 1.0));
	return output;
}
