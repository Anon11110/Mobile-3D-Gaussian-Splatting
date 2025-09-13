#version 450

// Simple vertex shader for drawing a wireframe sphere.
// It transforms the sphere's vertices using a Model-View-Projection matrix.

layout(location = 0) in vec3 inPosition;

// Uniform Buffer Object containing the combined MVP matrix
// for the specific sphere instance being drawn.
layout(set = 0, binding = 0) uniform DebugUBO
{
	mat4 mvp;
}
ubo;

void main()
{
	// Transform the vertex position by the MVP matrix.
	gl_Position = ubo.mvp * vec4(inPosition, 1.0);
}
