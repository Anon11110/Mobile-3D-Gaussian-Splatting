#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 in_pos;        // Quad corner vertex position [-1, 1]

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_uv;

layout(set = 0, binding = 0) uniform FrameUBO
{
	mat4 view_projection;
	vec4 camera_pos;
	vec2 viewport;
	vec2 focal;
}
ubo;

// AoS Buffer Layout: Interleaved splat attributes
struct SplatAttributes
{
	vec4 position;        // xyz + padding
	vec4 scale;           // xyz + padding
	vec4 rotation;        // quaternion xyzw
	vec4 color;           // rgba (with opacity in alpha)
};

layout(set = 0, binding = 1, std430) readonly buffer SplatAttributesBuffer
{
	SplatAttributes attributes[];
}
splat_attributes_buffer;

layout(set = 0, binding = 2, std430) readonly buffer SH_Coefficients
{
	float sh_coeffs[];
}
sh_coefficients_buffer;

layout(set = 0, binding = 3, std430) readonly buffer SortedIndices
{
	uint indices[];
}
sorted_indices_buffer;

void main()
{
	uint splat_index = sorted_indices_buffer.indices[gl_InstanceIndex];

	// Fetch all attributes in one memory access (AoS layout)
	SplatAttributes splat = splat_attributes_buffer.attributes[splat_index];

	vec3  pos   = splat.position.xyz;
	vec3  scale = splat.scale.xyz;
	vec4  rot   = splat.rotation;

	// --- Vertex Transformation ---
	vec4 splat_center_cam = ubo.view_projection * vec4(pos, 1.0);

	// Basic billboard - a more correct implementation would use the covariance matrix
	gl_Position = splat_center_cam + vec4(in_pos * 0.05, 0.0, 0.0);

	// --- Color Output ---
	// Color is pre-computed and stored directly in the attributes buffer
	out_color = splat.color;
	out_uv    = in_pos;
}
