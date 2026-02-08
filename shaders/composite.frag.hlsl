[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]] Texture2D<float4> accumTexture;
[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]] SamplerState linearSampler;

struct CompositePC
{
    float4 backgroundColor;
};
[[vk::push_constant]] CompositePC pc;

struct PSInput
{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
    float4 accum = accumTexture.Sample(linearSampler, input.uv);

    float3 background = pc.backgroundColor.rgb;
    float3 finalColor = accum.rgb + background * accum.a;

    return float4(finalColor, 1.0);
}
