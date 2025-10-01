#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main()
{
	// Create a soft, circular splat shape
	float dist_sq = dot(in_uv, in_uv);
	if (dist_sq > 1.0)
	{
		discard;
	}

	// Apply Gaussian falloff to the alpha
	float alpha = in_color.a * exp(-0.5 * dist_sq);

	out_color = vec4(in_color.rgb, alpha);
}
