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

layout(set = 0, binding = 1, std430) readonly buffer PositionsBuffer
{
	vec3 positions[];
};

layout(set = 0, binding = 2, std430) readonly buffer ScalesBuffer
{
	vec3 scales[];
};

layout(set = 0, binding = 3, std430) readonly buffer RotationsBuffer
{
	vec4 rotations[];
};

layout(set = 0, binding = 4, std430) readonly buffer ColorsBuffer
{
	vec4 colors[];
};

layout(set = 0, binding = 5, std430) readonly buffer SHRestBuffer
{
	float shRest[];
};

layout(set = 0, binding = 6, std430) readonly buffer SortedIndicesBuffer
{
	uint indices[];
};

void main()
{
	uint splat_index = indices[gl_InstanceIndex];

	vec3  pos   = positions[splat_index];
	vec3  scale = scales[splat_index];
	vec4  rot   = rotations[splat_index];
	vec4  color = colors[splat_index];

	// --- Vertex Transformation ---
	vec4 splat_center_cam = ubo.view_projection * vec4(pos, 1.0);

	// Basic billboard - a more correct implementation would use the covariance matrix
	gl_Position = splat_center_cam + vec4(in_pos * 0.05, 0.0, 0.0);

	// --- Color Output ---
	// Color is pre-computed and stored directly in the colors buffer
	out_color = color;
	out_uv    = in_pos;
}
