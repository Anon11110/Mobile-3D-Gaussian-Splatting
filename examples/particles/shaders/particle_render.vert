#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform MVP
{
    mat4 mvp;
} ubo;

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
    gl_PointSize = 3.0;

    // Color based on particle height
    float normalizedHeight = (inPosition.y + 1.0) / 2.0;
    fragColor = mix(vec3(1.0, 0.8, 0.2), vec3(0.2, 0.4, 1.0), normalizedHeight);
}