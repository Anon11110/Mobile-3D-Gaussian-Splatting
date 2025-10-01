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

// SoA Buffer Layout
layout(set = 0, binding = 1, std430) readonly buffer Positions
{
	vec3 positions[];
}
positions_buffer;
layout(set = 0, binding = 2, std430) readonly buffer Scales
{
	vec3 scales[];
}
scales_buffer;
layout(set = 0, binding = 3, std430) readonly buffer Rotations
{
	vec4 rotations[];
}
rotations_buffer;
layout(set = 0, binding = 4, std430) readonly buffer SH_Coefficients
{
	float sh_coeffs[];
}
sh_coefficients_buffer;
layout(set = 0, binding = 5, std430) readonly buffer Opacities
{
	float opacities[];
}
opacities_buffer;
layout(set = 0, binding = 6, std430) readonly buffer SortedIndices
{
	uint indices[];
}
sorted_indices_buffer;

// Basic SH evaluation (degree 0)
vec3 eval_sh_deg0(in float sh_c0)
{
	const float SH_C0 = 0.28209479177387814;
	return sh_c0 * vec3(SH_C0);
}

void main()
{
	uint splat_index = sorted_indices_buffer.indices[gl_InstanceIndex];

	// Fetch attributes from SoA buffers
	vec3  pos     = positions_buffer.positions[splat_index];
	vec3  scale   = scales_buffer.scales[splat_index];
	vec4  rot     = rotations_buffer.rotations[splat_index];
	float opacity = opacities_buffer.opacities[splat_index];

	// --- Vertex Transformation ---
	vec4 splat_center_cam = ubo.view_projection * vec4(pos, 1.0);

	// Basic billboard - a more correct implementation would use the covariance matrix
	gl_Position = splat_center_cam + vec4(in_pos * 0.05, 0.0, 0.0);

	// --- Color Calculation (simplified) ---
	// Evaluate SH degree 0 for base color
	// The SH buffer is a flat array, so we need to calculate the offset
	float sh_c0_r = sh_coefficients_buffer.sh_coeffs[splat_index * 16 * 3 + 0];
	float sh_c0_g = sh_coefficients_buffer.sh_coeffs[splat_index * 16 * 3 + 16];
	float sh_c0_b = sh_coefficients_buffer.sh_coeffs[splat_index * 16 * 3 + 32];

	vec3 color = vec3(eval_sh_deg0(sh_c0_r).r, eval_sh_deg0(sh_c0_g).g, eval_sh_deg0(sh_c0_b).b);

	out_color = vec4(color, opacity);
	out_uv    = in_pos;
}
