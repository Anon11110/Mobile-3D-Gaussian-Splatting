#version 450

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    // Simple circular point sprite
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);

    if (dist > 0.5)
        discard;

    // Soft edges
    float alpha = 1.0 - smoothstep(0.3, 0.5, dist);

    outColor = vec4(fragColor, alpha);
}