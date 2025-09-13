#version 450

// Simple fragment shader that outputs a solid bright green color.
// This is used for the wireframe debug spheres to make them easily visible.

layout(location = 0) out vec4 outColor;

void main()
{
	// Output a constant, bright green color for the wireframe lines.
	outColor = vec4(1.0, 1.0, 1.0, 1.0);
}
