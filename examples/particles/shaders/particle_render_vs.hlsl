struct VSInput
{
	[[vk::location(0)]] float3 position : POSITION;
};

struct VSOutput
{
	float4 position : SV_Position;
	nointerpolation float emitterID : TEXCOORD0;
	float3 worldPosition : TEXCOORD1;
	float pointSize : PSIZE;
};

struct MVP
{
	float4x4 mvp;
};

[[vk::binding(0, 0)]]
ConstantBuffer<MVP> ubo : register(b0);

VSOutput main(VSInput input, uint vertexIndex : SV_VertexID)
{
	VSOutput output;
	output.position = mul(ubo.mvp, float4(input.position, 1.0));

	// Make particles small for a fine smoke effect
	output.pointSize = 2.0;

	// Determine emitter ID from the vertex index and pass to fragment shader
	output.emitterID = float(vertexIndex & 1u);
	output.worldPosition = input.position;

	return output;
}
