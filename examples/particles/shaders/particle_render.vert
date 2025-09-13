#version 450

layout(location = 0) in vec3 inPosition;

// We pass the emitter ID (0 or 1) to the fragment shader.
// 'flat' ensures the value is not interpolated for point primitives.
layout(location = 0) out flat float fragEmitterID;
layout(location = 1) out vec3 fragPosition;

layout(set = 0, binding = 0) uniform MVP
{
	mat4 mvp;
}
ubo;

void main()
{
	gl_Position = ubo.mvp * vec4(inPosition, 1.0);

	// Make particles small for a fine smoke effect
	gl_PointSize = 2.0;

	// Determine emitter ID from the vertex index and pass to fragment shader
	fragEmitterID = float(gl_VertexIndex & 1u);
	fragPosition  = inPosition;
}
