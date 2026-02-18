#include "shaderio.h"

[[vk::binding(0, 0)]] ConstantBuffer<FrameUBO> ubo;
[[vk::binding(1, 0)]] StructuredBuffer<uint> indices;
[[vk::binding(2, 0)]] StructuredBuffer<HWRasterSplat> preprocessedSplats;

struct VSOutput
{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] noperspective float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;

    uint splatId    = instanceId;
    uint splatIndex = indices[splatId];

    HWRasterSplat splat = preprocessedSplats[splatIndex];

    // Triangle strip "Z" pattern mapping
    float2 cornerUnit;
    switch (vertexId)
    {
        case 0:
            cornerUnit = float2(-1.0, -1.0);  // Bottom-Left
            break;
        case 1:
            cornerUnit = float2(1.0, -1.0);   // Bottom-Right
            break;
        case 2:
            cornerUnit = float2(-1.0, 1.0);   // Top-Left
            break;
        default:  // case 3
            cornerUnit = float2(1.0, 1.0);    // Top-Right
            break;
    }

    // NDC basis vectors are pre-rotated and pre-scaled by the preprocess shader
    float2 ndcOffset = cornerUnit.x * splat.ndcBasis1 + cornerUnit.y * splat.ndcBasis2;
    float2 clipOffset = ndcOffset * splat.centerClip.w;

    output.position = splat.centerClip + float4(clipOffset, 0.0, 0.0);
    output.color    = splat.color;
    output.uv       = cornerUnit;

    return output;
}
