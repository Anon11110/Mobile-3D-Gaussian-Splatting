#version 450

// Vertex inputs
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// Uniform buffer (set 0, binding 0)
layout(set = 0, binding = 0) uniform UniformBufferObject
{
	mat4  mvp;
	float time;
	float padding[3];
}
ubo;

// Push constants: [0]=centerNdc.x, [1]=centerNdc.y, [2]=rotation (rad), [3]=aspect = width/height
layout(push_constant) uniform PushConstants
{
	vec2  centerNdc;
	float rotation;
	float aspect;
}
pc;

// Output to fragment shader
layout(location = 0) out vec3 fragColor;

void main()
{
	// To clip space
	vec4  clip = ubo.mvp * vec4(inPosition, 1.0);
	float w    = clip.w;

	// To NDC
	vec2 ndc = clip.xy / w;

	// Rotate in NDC about the projected centroid, with aspect compensation
	vec2 p = ndc - pc.centerNdc;
	p.x *= pc.aspect;        // pre-scale X so rotation is circular on screen

	float c = cos(pc.rotation);
	float s = sin(pc.rotation);
	vec2  r = vec2(p.x * c - p.y * s, p.x * s + p.y * c);

	r.x /= pc.aspect;        // undo compensation
	vec2 ndcOut = pc.centerNdc + r;

	// Back to clip space
	gl_Position = vec4(ndcOut * w, clip.z, w);

	// Animate color based on time from uniform buffer
	vec3 col = inColor;
	col.r *= (0.5 + 0.5 * sin(ubo.time));
	col.g *= (0.5 + 0.5 * cos(ubo.time * 1.2));
	col.b *= (0.5 + 0.5 * sin(ubo.time * 0.8));
	fragColor = col;
}