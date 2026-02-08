struct VSOutput
{
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
