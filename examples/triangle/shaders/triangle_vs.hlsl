// ENTRYPOINT: vs_main
struct VSInput
{
    float3 inPosition : TEXCOORD0;
    float3 inColor : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
};

cbuffer UniformBufferObject : register(b0)
{
    float4x4 ubo_mvp;
    float ubo_time;
    float3 ubo_padding;
};

// Push constants: Vulkan uses struct with [[vk::push_constant]], Metal3 uses cbuffer
#ifdef VULKAN
struct PushConstantData
{
    float2 centerNdc;
    float rotation;
    float aspect;
};
[[vk::push_constant]] PushConstantData pc;
#define PC_CENTER_NDC pc.centerNdc
#define PC_ROTATION pc.rotation
#define PC_ASPECT pc.aspect
#else
cbuffer PushConstants : register(b30)
{
    float2 pc_centerNdc;
    float pc_rotation;
    float pc_aspect;
};
#define PC_CENTER_NDC pc_centerNdc
#define PC_ROTATION pc_rotation
#define PC_ASPECT pc_aspect
#endif

VSOutput vs_main(VSInput input)
{
    VSOutput output;

    float4 clip = mul(ubo_mvp, float4(input.inPosition, 1.0f));
    float w = clip.w;

    float2 ndc = clip.xy / w;

    float2 p = ndc - PC_CENTER_NDC;
    p.x *= PC_ASPECT;

    float c = cos(PC_ROTATION);
    float s = sin(PC_ROTATION);
    float2 r = float2(p.x * c - p.y * s, p.x * s + p.y * c);

    r.x /= PC_ASPECT;
    float2 ndcOut = PC_CENTER_NDC + r;

    output.position = float4(ndcOut * w, clip.z, w);

    float3 col = input.inColor;
    col.r *= 0.5f + 0.5f * sin(ubo_time);
    col.g *= 0.5f + 0.5f * cos(ubo_time * 1.2f);
    col.b *= 0.5f + 0.5f * sin(ubo_time * 0.8f);
    output.color = col;

    return output;
}
