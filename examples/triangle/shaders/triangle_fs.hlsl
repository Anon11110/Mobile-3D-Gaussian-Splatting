// ENTRYPOINT: fs_main
struct PSInput
{
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
};

float4 fs_main(PSInput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
