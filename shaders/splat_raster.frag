#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 inColor;
layout(location = 1) noperspective in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
	// Create a soft, circular splat shape
	float distSq = dot(inUV, inUV);

	if (distSq > 1.0)
	{
		discard;
	}

	// Scale by 8.0 to match the sqrt(8) expansion in vertex shader
	float alpha = inColor.a * exp(-0.5 * distSq * 8.0);

	outColor = vec4(inColor.rgb, alpha);
}
