struct PSInput
{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] noperspective float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
    // Create a soft, circular splat shape
    float distSq = dot(input.uv, input.uv);

    if (distSq > 1.0)
    {
        discard;
    }

    // Scale by 8.0 to match the sqrt(8) expansion in vertex shader
    float alpha = input.color.a * exp(-0.5 * distSq * 8.0);

    return float4(input.color.rgb, alpha);
}
